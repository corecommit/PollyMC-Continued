/* Copyright 2026 PollyMC-Continued Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CommandPalette.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QShowEvent>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceList.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "ui/MainWindow.h"
#include "voice/ModelDownloader.h"
#include "voice/VoiceIntentMatcher.h"

// Score: higher wins. Exact prefix > word boundary > substring > scattered.
static int fuzzyScore(const QString& needle, const QString& haystack, bool& matched)
{
    matched = false;
    if (needle.isEmpty())
        return 0;
    const QString n = needle.toLower();
    const QString h = haystack.toLower();
    if (h.startsWith(n)) {
        matched = true;
        return 4000 - h.length();
    }
    // word boundary: match right after a space, -, _ or /
    int pos = 0;
    while ((pos = h.indexOf(n, pos)) != -1) {
        const QChar prev = pos > 0 ? h[pos - 1] : QChar(' ');
        if (prev == ' ' || prev == '-' || prev == '_' || prev == '/') {
            matched = true;
            return 3000 - pos;
        }
        pos += n.length();
    }
    if (h.contains(n)) {
        matched = true;
        return 2000 - h.indexOf(n);
    }
    // scattered: chars in order, penalize gaps
    int hi = 0, gaps = 0;
    for (const QChar c : n) {
        const int found = h.indexOf(c, hi);
        if (found == -1)
            return -1;
        gaps += found - hi;
        hi = found + 1;
    }
    matched = true;
    return 1000 - qMin(gaps, 999);
}

// Icon + text left, shortcut in a fixed right column, substring bolded.
class ShortcutDelegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setQuery(const QString& query) { m_query = query; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        if (option.state & QStyle::State_Selected)
            painter->fillRect(option.rect, option.palette.highlight());
        const bool enabled = index.data(Qt::UserRole + 2).toBool();
        const bool selected = option.state & QStyle::State_Selected;
        const QColor textColor = selected ? option.palette.highlightedText().color()
                                          : enabled ? option.palette.text().color()
                                                    : option.palette.color(QPalette::Disabled, QPalette::Text);
        const int pad = 6;
        const int iconSlot = 22;
        const int shortcutCol = 120;
        const QRect content = option.rect.adjusted(pad, 0, -pad, 0);
        // fixed icon slot keeps text aligned whether or not an icon exists
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull())
            icon.paint(painter, QRect(content.left(), content.center().y() - 8, 16, 16));
        // fixed right column keeps shortcuts aligned across row widths
        const QString shortcut = index.data(Qt::UserRole + 1).toString();
        if (!shortcut.isEmpty()) {
            painter->setPen(selected ? option.palette.highlightedText().color()
                                     : option.palette.color(QPalette::Disabled, QPalette::Text));
            painter->drawText(QRect(content.right() - shortcutCol, content.top(), shortcutCol, content.height()),
                              Qt::AlignRight | Qt::AlignVCenter, shortcut);
        }
        QRect textRect(content.left() + iconSlot, content.top(), content.width() - iconSlot - shortcutCol - pad,
                       content.height());
        textRect = QStyle::visualRect(option.direction, content, textRect);
        const QFontMetrics fm(option.font);
        const QString text = fm.elidedText(index.data().toString(), Qt::ElideRight, textRect.width());
        int matchAt = -1;
        if (!m_query.isEmpty())
            matchAt = text.indexOf(m_query, 0, Qt::CaseInsensitive);
        painter->setPen(textColor);
        if (matchAt < 0) {
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
        } else {
            const int y = textRect.top() + fm.ascent() + (textRect.height() - fm.height()) / 2;
            int x = textRect.left();
            painter->drawText(x, y, text.left(matchAt));
            x += fm.horizontalAdvance(text.left(matchAt));
            QFont bold(option.font);
            bold.setBold(true);
            painter->setFont(bold);
            if (!selected)
                painter->setPen(option.palette.highlight().color());
            painter->drawText(x, y, text.mid(matchAt, m_query.length()));
            x += QFontMetrics(bold).horizontalAdvance(text.mid(matchAt, m_query.length()));
            painter->setFont(option.font);
            painter->setPen(textColor);
            painter->drawText(x, y, text.mid(matchAt + m_query.length()));
        }
        painter->restore();
    }

   private:
    QString m_query;
};

class CommandPalette::Model : public QAbstractListModel {
   public:
    explicit Model(QList<CommandDescriptor> entries, QObject* parent = nullptr) : QAbstractListModel(parent), m_entries(std::move(entries)) {}
    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_entries.size() + (m_hasSuggestion ? 1 : 0);
    }
    void setSuggestion(const CommandDescriptor& entry)
    {
        beginResetModel();
        m_suggestion = entry;
        m_hasSuggestion = true;
        endResetModel();
    }
    void clearSuggestion()
    {
        if (!m_hasSuggestion)
            return;
        beginResetModel();
        m_hasSuggestion = false;
        endResetModel();
    }
    bool hasSuggestion() const { return m_hasSuggestion; }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return {};
        const CommandDescriptor& e = entryAt(index.row());
        if (role == Qt::DisplayRole)
            return e.text;
        if (role == Qt::DecorationRole)
            return e.sourceAction ? e.sourceAction->icon() : QIcon();
        if (role == Qt::UserRole + 1)
            return e.shortcut.toString();
        // enabled state is queried lazily on every paint, never cached
        if (role == Qt::UserRole + 2)
            return e.isEnabled ? e.isEnabled() : true;
        return {};
    }
    const CommandDescriptor& entryAt(int row) const
    {
        if (m_hasSuggestion)
            return row == 0 ? m_suggestion : m_entries.at(row - 1);
        return m_entries.at(row);
    }
    const QList<CommandDescriptor>& allEntries() const { return m_entries; }

   private:
    QList<CommandDescriptor> m_entries;
    CommandDescriptor m_suggestion;
    bool m_hasSuggestion = false;
};

class CommandPalette::FuzzyFilter : public QSortFilterProxyModel {
   public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(const QString& query) { m_query = query; }
    const QString& query() const { return m_query; }
    // Public entry for outside code to re-run the row filter after
    // m_query changes. Qt 6 made the invalidate* methods protected,
    // so callers must go through a public wrapper.
    void reapplyFilter() { invalidateRowsFilter(); }

   protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override
    {
        Q_UNUSED(parent);
        const auto* model = static_cast<const Model*>(sourceModel());
        if (model->hasSuggestion() && row == 0)
            return true;
        const CommandDescriptor& e = model->entryAt(row);
        if (e.hideByDefault) {
            // hidden unless the query matches at word boundary or better
            if (m_query.isEmpty())
                return false;
            bool matched = false;
            const int score = fuzzyScore(m_query, e.text, matched);
            return matched && score >= 2000;
        }
        if (m_query.isEmpty())
            return true;
        bool matched = false;
        fuzzyScore(m_query, e.text, matched);
        return matched;
    }
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        const auto* model = static_cast<const Model*>(sourceModel());
        // synthetic suggestion row always sorts first
        if (model->hasSuggestion()) {
            if (left.row() == 0 && right.row() != 0)
                return true;
            if (right.row() == 0 && left.row() != 0)
                return false;
        }
        if (m_query.isEmpty()) {
            // curated shortlist: prioritized actions first, rest in order
            const int pl = model->entryAt(left.row()).priority;
            const int pr = model->entryAt(right.row()).priority;
            if (pl != pr)
                return pl > pr;
            return left.row() < right.row();
        }
        bool ml = false, mr = false;
        const int sl = fuzzyScore(m_query, sourceModel()->data(left).toString(), ml) + model->entryAt(left.row()).weight;
        const int sr = fuzzyScore(m_query, sourceModel()->data(right).toString(), mr) + model->entryAt(right.row()).weight;
        if (sl != sr)
            return sl > sr;  // best score first
        const int pl = model->entryAt(left.row()).priority;
        const int pr = model->entryAt(right.row()).priority;
        if (pl != pr)
            return pl > pr;
        return left.row() < right.row();
    }

   private:
    QString m_query;
};

CommandPalette::CommandPalette(QList<CommandDescriptor> entries, MainWindow* mainWindow, QWidget* parent)
    : QDialog(parent), m_mainWindow(mainWindow)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setMinimumSize(560, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Type a command..."));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumHeight(32);
    QFont searchFont = m_search->font();
    searchFont.setPointSize(searchFont.pointSize() + 2);
    m_search->setFont(searchFont);
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_badge = new QLabel(tr("Natural language"), this);
    m_badge->setVisible(false);
    layout->addWidget(m_badge);

    m_count = new QLabel(this);
    m_count->setAlignment(Qt::AlignRight);
    m_count->setVisible(false);
    layout->addWidget(m_count);

    m_model = new Model(std::move(entries), this);
    m_filter = new FuzzyFilter(this);
    m_filter->setSourceModel(m_model);

    m_delegate = new ShortcutDelegate(this);
    m_list = new QListView(this);
    m_list->setModel(m_filter);
    m_list->setItemDelegate(m_delegate);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->installEventFilter(this);
    layout->addWidget(m_list, 1);
    m_filter->reapplyFilter();
    m_filter->sort(0);  // curated shortlist before the first keystroke

    auto* hint = new QLabel(tr("↑↓ navigate · PgUp/PgDn jump · Enter run · Esc close"), this);
    hint->setEnabled(false);
    layout->addWidget(hint);
    // TODO: add a categories line here once group headers land

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::onQueryChanged);
    connect(m_list, &QListView::activated, this, &CommandPalette::runSelected);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(400);
    connect(m_debounce, &QTimer::timeout, this, &CommandPalette::runIntentMatcher);
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::showEvent(QShowEvent* event)
{
    placeOverParent();
    QDialog::showEvent(event);
    m_search->setFocus();
    selectFirstRow();
    qApp->installEventFilter(this);
}

void CommandPalette::hideEvent(QHideEvent* event)
{
    qApp->removeEventFilter(this);
    QDialog::hideEvent(event);
}

void CommandPalette::placeOverParent()
{
    if (const QWidget* p = parentWidget()) {
        const int w = qMin(720, qMax(560, p->width() * 3 / 5));
        resize(w, 460);
        move(p->mapToGlobal(QPoint((p->width() - w) / 2, int(p->height() * 0.15))));
    }
}

bool CommandPalette::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (obj == m_search || obj == m_list) {
            const int row = m_list->currentIndex().row();
            if (key->key() == Qt::Key_Down) {
                m_list->setCurrentIndex(m_filter->index(qMin(row + 1, m_filter->rowCount() - 1), 0));
                return true;
            }
            if (key->key() == Qt::Key_Up) {
                m_list->setCurrentIndex(m_filter->index(qMax(row - 1, 0), 0));
                return true;
            }
            if (key->key() == Qt::Key_Home) {
                m_list->setCurrentIndex(m_filter->index(0, 0));
                return true;
            }
            if (key->key() == Qt::Key_End) {
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(m_filter->rowCount() - 1, 0));
                return true;
            }
            if (key->key() == Qt::Key_PageDown) {
                const int next = qMin(m_list->currentIndex().row() + 10, m_filter->rowCount() - 1);
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(next, 0));
                return true;
            }
            if (key->key() == Qt::Key_PageUp) {
                const int prev = qMax(m_list->currentIndex().row() - 10, 0);
                if (m_filter->rowCount() > 0)
                    m_list->setCurrentIndex(m_filter->index(prev, 0));
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                runSelected();
                return true;
            }
            if (key->key() == Qt::Key_Tab && m_naturalLanguageMode && m_model->hasSuggestion()) {
                m_list->setCurrentIndex(m_filter->index(0, 0));
                return true;
            }
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            QWidget* w = qobject_cast<QWidget*>(obj);
            if (w && w != this && !isAncestorOf(w)) {
                reject();
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

void CommandPalette::onQueryChanged(const QString& query)
{
    m_model->clearSuggestion();
    // NOTE: '?' is always the mode prefix, even mid-query. A command
    // whose name genuinely starts with '?' cannot be found this way;
    // that is accepted (no launcher action does).
    if (!query.isEmpty() && query.startsWith('?')) {
        m_naturalLanguageMode = true;
        m_badge->setVisible(true);
        m_strippedQuery = query.mid(1);
        m_filter->setQuery(m_strippedQuery);
        m_debounce->start();
    } else {
        m_naturalLanguageMode = false;
        m_badge->setVisible(false);
        m_strippedQuery.clear();
        m_filter->setQuery(query);
    }
    m_filter->sort(0);
    m_delegate->setQuery(m_naturalLanguageMode ? m_strippedQuery : query);
    m_count->setText(tr("%1 of %2").arg(m_filter->rowCount()).arg(m_model->rowCount()));
    m_count->setVisible(!query.isEmpty());
    m_list->viewport()->update();
    if (!m_naturalLanguageMode)
        selectFirstRow();
}

void CommandPalette::runIntentMatcher()
{
    if (!m_naturalLanguageMode)
        return;
    const QString query = m_strippedQuery.trimmed();
    if (query.isEmpty()) {
        // bare "?": hint row, matcher stays idle
        CommandDescriptor hint;
        hint.text = tr("Type your query...");
        hint.isEnabled = [] { return false; };
        setSuggestionRow(hint);
        return;
    }
    if (!m_downloader) {
        m_downloader = new ModelDownloader(this);
        connect(m_downloader, &ModelDownloader::ready, this, &CommandPalette::onModelsReady);
        connect(m_downloader, &ModelDownloader::failed, this, &CommandPalette::onModelsFailed);
    }
    if (!m_matcher) {
        m_matcher = new VoiceIntentMatcher(this);
        connect(m_matcher, &VoiceIntentMatcher::loaded, this, &CommandPalette::onMatcherReady);
        connect(m_matcher, &VoiceIntentMatcher::loadFailed, this, &CommandPalette::onMatcherFailed);
    }
    if (!m_modelsReady) {
        CommandDescriptor waiting;
        waiting.text = tr("Preparing language model...");
        waiting.isEnabled = [] { return false; };
        setSuggestionRow(waiting);
        m_downloader->ensureModelsPresent(this);
        return;
    }
    if (!m_matcher->isLoaded()) {
        CommandDescriptor waiting;
        waiting.text = tr("Preparing language model...");
        waiting.isEnabled = [] { return false; };
        setSuggestionRow(waiting);
        m_matcher->load(m_modelDir + "/intent-model-int8.onnx", m_modelDir + "/embeddings.json");
        return;
    }
    matchAndSuggest(query);
}

void CommandPalette::setSuggestionRow(const CommandDescriptor& entry)
{
    m_model->setSuggestion(entry);
    m_filter->sort(0);
    m_list->viewport()->update();
}

namespace {
// Strip leading action verbs so "launch all the mods" matches the instance.
QString stripLaunchVerbs(const QString& query)
{
    static const QStringList verbs = { "launch", "start", "play", "open" };
    QString out = query.trimmed();
    for (const QString& verb : verbs) {
        if (out.startsWith(verb, Qt::CaseInsensitive) &&
            (out.length() == verb.length() || out.at(verb.length()).isSpace())) {
            out = out.mid(verb.length()).trimmed();
            break;
        }
    }
    return out;
}

struct InstanceTarget {
    QString id;
    QString name;
    int score = -1;
    bool found = false;
};

InstanceTarget fuzzyInstance(const QString& text)
{
    InstanceTarget best;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        auto* inst = list->at(i);
        bool matched = false;
        const int score = fuzzyScore(text, inst->name(), matched);
        if (matched && score > best.score) {
            best.id = inst->id();
            best.name = inst->name();
            best.score = score;
            best.found = true;
        }
    }
    return best;
}

// Minecraft version match, only when the list is already loaded
// (never triggers a network fetch from the palette).
QString fuzzyVersion(const QString& text)
{
    auto list = APPLICATION->metadataIndex()->get("net.minecraft");
    if (!list || !list->isLoaded())
        return {};
    QString best;
    int bestScore = -1;
    for (int i = 0; i < list->count(); i++) {
        const QString ver = list->at(i)->descriptor();
        bool matched = false;
        const int score = fuzzyScore(text, ver, matched);
        if (matched && score > bestScore) {
            bestScore = score;
            best = ver;
        }
    }
    return best;
}
}  // namespace

void CommandPalette::onModelsReady(const QString& modelDir)
{
    m_modelsReady = true;
    m_modelDir = modelDir;
    if (!m_naturalLanguageMode || !m_matcher)
        return;
    if (m_matcher->isLoaded())
        matchAndSuggest(m_strippedQuery.trimmed());
    else
        m_matcher->load(m_modelDir + "/intent-model-int8.onnx", m_modelDir + "/embeddings.json");
}

void CommandPalette::onModelsFailed(const QString& reason)
{
    if (!m_naturalLanguageMode)
        return;
    CommandDescriptor msg;
    if (reason == "declined") {
        msg.text = tr("Model download declined. Natural language search needs a one-time download; type ? again to retry.");
    } else {
        msg.text = tr("Language model unavailable (%1).").arg(reason);
    }
    msg.isEnabled = [] { return false; };
    setSuggestionRow(msg);
}

void CommandPalette::onMatcherReady()
{
    if (m_naturalLanguageMode && m_matcher)
        matchAndSuggest(m_strippedQuery.trimmed());
}

void CommandPalette::onMatcherFailed(const QString& reason)
{
    Q_UNUSED(reason);
    if (!m_naturalLanguageMode)
        return;
    // Corrupt download: drop the model file so the next attempt
    // re-downloads instead of failing the same way forever.
    // Embeddings/tokenizer stay; only the ONNX blob is suspect.
    QFile::remove(m_modelDir + "/intent-model-int8.onnx");
    m_modelsReady = false;
    CommandDescriptor msg;
    msg.text = tr("Language model file was corrupted and removed. Type ? again to re-download.");
    msg.isEnabled = [] { return false; };
    setSuggestionRow(msg);
}

void CommandPalette::matchAndSuggest(const QString& query)
{
    if (!m_naturalLanguageMode || !m_matcher || !m_matcher->isLoaded() || query.isEmpty())
        return;
    const auto result = m_matcher->match(query);
    if (!result.matched) {
        CommandDescriptor empty;
        empty.text = tr("No matching command found. Try typing without '?' to search by name.");
        empty.isEnabled = [] { return false; };
        setSuggestionRow(empty);
        return;
    }
    const CommandDescriptor* action = nullptr;
    for (const auto& e : m_model->allEntries()) {
        if (e.id == result.id) {
            action = &e;
            break;
        }
    }
    if (!action) {
        CommandDescriptor empty;
        empty.text = tr("No matching command found. Try typing without '?' to search by name.");
        empty.isEnabled = [] { return false; };
        setSuggestionRow(empty);
        return;
    }
    const int pct = int(result.similarity * 100.0f + 0.5f);
    if (result.id == "actionLaunchInstance" || result.id == "actionKillInstance" ||
        result.id == "actionEditInstance" || result.id == "actionDeleteInstance") {
        const InstanceTarget target = fuzzyInstance(stripLaunchVerbs(query));
        CommandDescriptor suggestion;
        if (target.found) {
            suggestion.text = tr("%1: %2 (%3%)").arg(action->text, target.name).arg(pct);
            const CommandDescriptor act = *action;
            const QString instId = target.id;
            MainWindow* mw = m_mainWindow;
            suggestion.isEnabled = act.isEnabled;
            suggestion.trigger = [mw, instId, act] {
                if (mw)
                    mw->triggerInstanceAction(instId, act.sourceAction);
            };
            setSuggestionRow(suggestion);
            return;
        }
        if (result.id == "actionLaunchInstance") {
            const QString ver = fuzzyVersion(stripLaunchVerbs(query));
            if (!ver.isEmpty()) {
                const CommandDescriptor* addAction = nullptr;
                for (const auto& e : m_model->allEntries()) {
                    if (e.id == "actionAddInstance") {
                        addAction = &e;
                        break;
                    }
                }
                if (addAction) {
                    suggestion.text = tr("New instance with %1 (%2%)").arg(ver).arg(pct);
                    suggestion.isEnabled = addAction->isEnabled;
                    suggestion.trigger = addAction->trigger;
                    setSuggestionRow(suggestion);
                    return;
                }
            }
        }
        suggestion.text =
            tr("The phrase doesn't clearly match one command. Try typing without '?' to fuzzy-search by name, or rephrase.");
        suggestion.isEnabled = [] { return false; };
        setSuggestionRow(suggestion);
        return;
    }
    CommandDescriptor suggestion;
    suggestion.text = tr("Intent: %1 (%2%)").arg(action->text).arg(pct);
    suggestion.isEnabled = action->isEnabled;
    suggestion.trigger = action->trigger;
    setSuggestionRow(suggestion);
}

void CommandPalette::selectFirstRow()
{
    if (m_filter->rowCount() > 0)
        m_list->setCurrentIndex(m_filter->index(0, 0));
}

void CommandPalette::runSelected()
{
    const QModelIndex proxyIndex = m_list->currentIndex();
    if (!proxyIndex.isValid())
        return;
    const CommandDescriptor entry = m_model->entryAt(m_filter->mapToSource(proxyIndex).row());
    if (entry.isEnabled && !entry.isEnabled())
        return;
    // close first so the action runs without the modal dialog in the way
    if (entry.closeAfterTrigger)
        accept();
    if (entry.trigger)
        QTimer::singleShot(0, entry.trigger);
}

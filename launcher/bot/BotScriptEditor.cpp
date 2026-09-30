#include "BotScriptEditor.h"

#include "BotManagerDialog.h"
#include "bot/BotScript.h"
#include "ui_BotScriptEditor.h"

#include <QMenu>
#include <QHash>
#include <QJsonDocument>
#include <QMimeData>
#include <QSignalBlocker>
#include <QTimer>

// Tree model over nested BotScriptStep lists. Structural edits reset
// the model; field edits emit dataChanged. No cross-level drops.
class StepModel : public QAbstractItemModel {
   public:
    struct Node {
        QList<BotScriptStep>* list = nullptr;
        int row = -1;
        Node* parent = nullptr;
    };

    explicit StepModel(QObject* parent = nullptr) : QAbstractItemModel(parent) {}
    void setRoot(QList<BotScriptStep>* root)
    {
        beginResetModel();
        m_root = root;
        qDeleteAll(m_nodeCache);
        m_nodeCache.clear();
        endResetModel();
    }

    BotScriptStep* stepAt(const QModelIndex& index) const
    {
        if (!index.isValid() || !m_root)
            return nullptr;
        auto* node = static_cast<Node*>(index.internalPointer());
        if (!node || !node->list || node->row < 0 || node->row >= node->list->size())
            return nullptr;
        return &(*node->list)[node->row];
    }

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override
    {
        if (column != 0 || !m_root)
            return {};
        QList<BotScriptStep>* list = m_root;
        Node* parentNode = nullptr;
        if (parent.isValid()) {
            parentNode = static_cast<Node*>(parent.internalPointer());
            if (!parentNode || parentNode->row < 0)
                return {};
            BotScriptStep& step = (*parentNode->list)[parentNode->row];
            if (step.type != "loop")
                return {};
            list = &step.steps;
        }
        if (row < 0 || row >= list->size())
            return {};
        const auto key = qMakePair(static_cast<const void*>(list), row);
        auto it = m_nodeCache.find(key);
        if (it != m_nodeCache.end()) {
            it.value()->parent = parentNode;
            return createIndex(row, column, it.value());
        }
        auto* node = new Node{ list, row, parentNode };
        m_nodeCache.insert(key, node);
        return createIndex(row, column, node);
    }

    QModelIndex parent(const QModelIndex& index) const override
    {
        if (!index.isValid())
            return {};
        auto* node = static_cast<Node*>(index.internalPointer());
        if (!node || !node->parent)
            return {};
        return createIndex(node->parent->row, 0, node->parent);
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (!m_root)
            return 0;
        if (!parent.isValid())
            return m_root->size();
        auto* node = static_cast<Node*>(parent.internalPointer());
        if (!node || node->row < 0)
            return 0;
        const BotScriptStep& step = (*node->list)[node->row];
        return step.type == "loop" ? step.steps.size() : 0;
    }

    int columnCount(const QModelIndex& = QModelIndex()) const override { return 1; }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        const BotScriptStep* step = stepAt(index);
        if (!step)
            return {};
        return displayText(*step);
    }

    QVariant headerData(int, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
            return tr("Step");
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto f = QAbstractItemModel::flags(index);
        if (index.isValid())
            f |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
        else
            f |= Qt::ItemIsDropEnabled;
        return f;
    }

    static QString displayText(const BotScriptStep& s)
    {
        if (s.type == "say" || s.type == "command" || s.type == "log")
            return s.type + ": " + s.text;
        if (s.type == "wait")
            return QString("wait %1s").arg(s.seconds);
        if (s.type == "wait_for_chat")
            return "wait for chat ~" + s.pattern;
        if (s.type == "wait_for_player")
            return "wait for " + s.player;
        if (s.type == "loop")
            return s.times == -1 ? "loop ∞" : QString("loop ×%1").arg(s.times);
        return s.type;
    }

    bool moveStep(const QModelIndex& parent, int from, int to)
    {
        QList<BotScriptStep>* list = listOf(parent);
        if (!list || from < 0 || from >= list->size() || to < 0 || to >= list->size() || from == to)
            return false;
        if (from < to) {
            beginMoveRows(parent, from, from, parent, to + 1);
            list->move(from, to);
        } else {
            beginMoveRows(parent, from, from, parent, to);
            list->move(from, to);
        }
        endMoveRows();
        return true;
    }

    void insertStep(const QModelIndex& parent, int row, const BotScriptStep& step)
    {
        QList<BotScriptStep>* list = listOf(parent);
        if (!list)
            return;
        row = qBound(0, row, list->size());
        beginInsertRows(parent, row, row);
        list->insert(row, step);
        endInsertRows();
    }

    void removeStep(const QModelIndex& index)
    {
        if (!index.isValid())
            return;
        auto* node = static_cast<Node*>(index.internalPointer());
        beginRemoveRows(parent(index), node->row, node->row);
        node->list->removeAt(node->row);
        endRemoveRows();
    }

    QStringList mimeTypes() const override { return { "application/x-qabstractitemmodeldatalist" }; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

    QMimeData* mimeData(const QModelIndexList& indexes) const override
    {
        m_dragSource.clear();
        for (const auto& idx : indexes) {
            if (idx.isValid() && idx.column() == 0)
                m_dragSource.append(idx);
        }
        auto* mime = new QMimeData();
        mime->setData("application/x-qabstractitemmodeldatalist", {});
        return mime;
    }

    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int, int, const QModelIndex&) const override
    {
        return action == Qt::MoveAction && data->hasFormat("application/x-qabstractitemmodeldatalist") &&
               !m_dragSource.isEmpty();
    }

    bool dropMimeData(const QMimeData*, Qt::DropAction action, int row, int, const QModelIndex& parent) override
    {
        if (action != Qt::MoveAction || m_dragSource.isEmpty())
            return false;
        const QModelIndex src = m_dragSource.first();
        m_dragSource.clear();
        if (src.parent() != parent)
            return false;  // siblings only, no cross-level drops
        int dest = (row == -1) ? rowCount(parent) : row;
        if (dest == src.row() || dest == src.row() + 1)
            return false;
        return moveStep(parent, src.row(), dest > src.row() ? dest - 1 : dest);
    }

   private:
    QList<BotScriptStep>* listOf(const QModelIndex& parent) const
    {
        if (!m_root)
            return nullptr;
        if (!parent.isValid())
            return m_root;
        auto* node = static_cast<Node*>(parent.internalPointer());
        if (!node || node->row < 0)
            return nullptr;
        BotScriptStep& step = (*node->list)[node->row];
        return step.type == "loop" ? &step.steps : nullptr;
    }

    QList<BotScriptStep>* m_root = nullptr;
    // nodes cached by (list, row); cleared on every structural reset
    mutable QHash<QPair<const void*, int>, Node*> m_nodeCache;
    mutable QModelIndexList m_dragSource;
};

static BotScriptStep defaultStep(const QString& type)
{
    BotScriptStep s;
    s.type = type;
    if (type == "say")
        s.text = "hello";
    else if (type == "command")
        s.text = "/help";
    else if (type == "log")
        s.text = "note";
    else if (type == "wait")
        s.seconds = 30;
    else if (type == "wait_for_chat")
        s.timeoutSeconds = 300;
    else if (type == "wait_for_player")
        s.timeoutSeconds = 600;
    else if (type == "loop")
        s.times = 10;
    return s;
}

static const char* STEP_TYPES[] = { "say", "command", "wait", "wait_for_chat", "wait_for_player", "log", "loop" };

BotScriptEditor::BotScriptEditor(BotManagerDialog* manager, QWidget* parent) : QWidget(parent), m_manager(manager), m_ui(new Ui::BotScriptEditor)
{
    m_ui->setupUi(this);

    auto* model = new StepModel(this);
    m_ui->stepsTree->setModel(model);
    m_ui->stepsTree->setDragDropMode(QAbstractItemView::InternalMove);
    m_ui->stepsTree->setSelectionMode(QAbstractItemView::SingleSelection);

    for (auto* label : { m_ui->textError, m_ui->waitError, m_ui->chatError, m_ui->playerError, m_ui->loopError })
        label->setStyleSheet("color: #f87171;");

    connect(m_ui->stepsTree->selectionModel(), &QItemSelectionModel::currentChanged, this, &BotScriptEditor::onSelectionChanged);
    connect(m_ui->addBtn, &QPushButton::clicked, this, &BotScriptEditor::onAddStep);
    connect(m_ui->removeBtn, &QPushButton::clicked, this, &BotScriptEditor::onRemoveStep);
    connect(m_ui->upBtn, &QPushButton::clicked, this, [this] { onMoveStep(-1); });
    connect(m_ui->downBtn, &QPushButton::clicked, this, [this] { onMoveStep(1); });
    connect(m_ui->nameEdit, &QLineEdit::textEdited, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->enabledCheck, &QCheckBox::toggled, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->textEdit, &QLineEdit::textEdited, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->secondsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->patternEdit, &QLineEdit::textEdited, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->caseCheck, &QCheckBox::toggled, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->timeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->playerEdit, &QLineEdit::textEdited, this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->timeoutSpin2, QOverload<int>::of(&QSpinBox::valueChanged), this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->timesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BotScriptEditor::onFieldEdited);
    connect(m_ui->saveBtn, &QPushButton::clicked, this, &BotScriptEditor::onSave);
    connect(m_ui->revertBtn, &QPushButton::clicked, this, &BotScriptEditor::onRevert);
    connect(m_ui->runBtn, &QPushButton::clicked, this, &BotScriptEditor::onRun);
    connect(m_ui->stopBtn, &QPushButton::clicked, this, &BotScriptEditor::onStop);
    connect(BotScriptStore::instance(), &BotScriptStore::changed, this, &BotScriptEditor::onStoreChanged);

    m_ui->propStack->setCurrentWidget(m_ui->pageEmpty);
    refreshButtons();
}

BotScriptEditor::~BotScriptEditor()
{
    delete m_ui;
}

static StepModel* treeModel(QTreeView* view)
{
    return static_cast<StepModel*>(view->model());
}

void BotScriptEditor::setBot(const QString& botName, const QString& scriptName, bool connected)
{
    m_botName = botName;
    m_connected = connected;
    auto* store = BotScriptStore::instance();
    if (!scriptName.isEmpty() && store->contains(scriptName)) {
        m_assignedScript = scriptName;
        rebuildFromScript(store->script(scriptName));
        m_saved = true;
    } else {
        m_assignedScript.clear();
        BotScript blank;
        blank.name = QString("script-%1").arg(m_nameCounter);
        while (store->contains(blank.name))
            blank.name = QString("script-%1").arg(++m_nameCounter);
        rebuildFromScript(blank);
        m_saved = false;
    }
    refreshButtons();
}

void BotScriptEditor::rebuildFromScript(const BotScript& script)
{
    QSignalBlocker block(m_ui->stepsTree);
    m_steps = script.steps;
    treeModel(m_ui->stepsTree)->setRoot(&m_steps);
    {
        QSignalBlocker b2(m_ui->nameEdit);
        m_ui->nameEdit->setText(script.name);
    }
    m_ui->propStack->setCurrentWidget(m_ui->pageEmpty);
    refreshButtons();
}

BotScript BotScriptEditor::currentScript() const
{
    BotScript script;
    script.version = 1;
    script.name = m_ui->nameEdit->text().trimmed();
    script.steps = m_steps;
    return script;
}

bool BotScriptEditor::validate(QString& error) const
{
    // reuse the exact parser rules by round-tripping
    BotScript built = currentScript();
    BotScript parsed;
    return BotScriptParser::parse(BotScriptParser::serialize(built), parsed, error);
}

void BotScriptEditor::onSelectionChanged()
{
    showPropertiesFor();
    refreshButtons();
}

void BotScriptEditor::showPropertiesFor()
{
    const QModelIndex current = m_ui->stepsTree->currentIndex();
    BotScriptStep* step = treeModel(m_ui->stepsTree)->stepAt(current);
    // clear all error labels first
    for (auto* label : { m_ui->textError, m_ui->waitError, m_ui->chatError, m_ui->playerError, m_ui->loopError })
        label->clear();
    if (!step) {
        m_ui->propStack->setCurrentWidget(m_ui->pageEmpty);
        return;
    }
    const QSignalBlocker b1(m_ui->textEdit), b2(m_ui->secondsSpin), b3(m_ui->patternEdit), b4(m_ui->caseCheck),
          b5(m_ui->timeoutSpin), b6(m_ui->playerEdit), b7(m_ui->timeoutSpin2), b8(m_ui->timesSpin);
    if (step->type == "say" || step->type == "command" || step->type == "log") {
        m_ui->propStack->setCurrentWidget(m_ui->pageText);
        m_ui->textEdit->setText(step->text);
    } else if (step->type == "wait") {
        m_ui->propStack->setCurrentWidget(m_ui->pageWait);
        m_ui->secondsSpin->setValue(step->seconds);
    } else if (step->type == "wait_for_chat") {
        m_ui->propStack->setCurrentWidget(m_ui->pageChat);
        m_ui->patternEdit->setText(step->pattern);
        m_ui->caseCheck->setChecked(step->caseSensitive);
        m_ui->timeoutSpin->setValue(step->timeoutSeconds);
    } else if (step->type == "wait_for_player") {
        m_ui->propStack->setCurrentWidget(m_ui->pagePlayer);
        m_ui->playerEdit->setText(step->player);
        m_ui->timeoutSpin2->setValue(step->timeoutSeconds);
    } else if (step->type == "loop") {
        m_ui->propStack->setCurrentWidget(m_ui->pageLoop);
        m_ui->timesSpin->setValue(step->times);
    }
}

void BotScriptEditor::onFieldEdited()
{
    m_ui->saveStatusLabel->setVisible(false);
    BotScriptStep* step = treeModel(m_ui->stepsTree)->stepAt(m_ui->stepsTree->currentIndex());
    if (step) {
        if (step->type == "say" || step->type == "command" || step->type == "log")
            step->text = m_ui->textEdit->text();
        else if (step->type == "wait")
            step->seconds = m_ui->secondsSpin->value();
        else if (step->type == "wait_for_chat") {
            step->pattern = m_ui->patternEdit->text();
            step->caseSensitive = m_ui->caseCheck->isChecked();
            step->timeoutSeconds = m_ui->timeoutSpin->value();
        } else if (step->type == "wait_for_player") {
            step->player = m_ui->playerEdit->text();
            step->timeoutSeconds = m_ui->timeoutSpin2->value();
        } else if (step->type == "loop")
            step->times = m_ui->timesSpin->value();
        const QModelIndex idx = m_ui->stepsTree->currentIndex();
        emit treeModel(m_ui->stepsTree)->dataChanged(idx, idx);
    }
    m_saved = false;
    refreshButtons();
}

void BotScriptEditor::onAddStep()
{
    QMenu menu(this);
    for (const char* t : STEP_TYPES)
        menu.addAction(QString(t));
    QAction* chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    auto* model = treeModel(m_ui->stepsTree);
    const QModelIndex current = m_ui->stepsTree->currentIndex();
    const QModelIndex parent = current.isValid() ? current.parent() : QModelIndex();
    int row = current.isValid() ? current.row() + 1 : model->rowCount(parent);
    // loops accept children when expanded; otherwise insert beside selection
    if (current.isValid()) {
        BotScriptStep* sel = model->stepAt(current);
        if (sel && sel->type == "loop" && m_ui->stepsTree->isExpanded(current)) {
            model->insertStep(current, model->rowCount(current), defaultStep(chosen->text()));
            m_ui->stepsTree->expand(current);
            m_saved = false;
            refreshButtons();
            return;
        }
    }
    model->insertStep(parent, row, defaultStep(chosen->text()));
    m_saved = false;
    refreshButtons();
}

void BotScriptEditor::onRemoveStep()
{
    const QModelIndex current = m_ui->stepsTree->currentIndex();
    if (!current.isValid())
        return;
    treeModel(m_ui->stepsTree)->removeStep(current);
    m_saved = false;
    refreshButtons();
}

void BotScriptEditor::onMoveStep(int direction)
{
    const QModelIndex current = m_ui->stepsTree->currentIndex();
    if (!current.isValid())
        return;
    auto* model = treeModel(m_ui->stepsTree);
    const int row = current.row();
    const int dest = row + direction;
    if (dest < 0 || dest >= model->rowCount(current.parent()))
        return;
    if (model->moveStep(current.parent(), row, dest)) {
        m_saved = false;
        refreshButtons();
    }
}

void BotScriptEditor::onSave()
{
    BotScript current = currentScript();
    QString error;
    BotScript parsed;
    if (!BotScriptParser::parse(BotScriptParser::serialize(current), parsed, error)) {
        refreshButtons();
        return;
    }
    auto* store = BotScriptStore::instance();
    if (!store->save(parsed, error)) {
        m_ui->saveStatusLabel->setText(tr("Save failed: %1").arg(error));
        m_ui->saveStatusLabel->setStyleSheet("color: #f87171;");
        m_ui->saveStatusLabel->setVisible(true);
        refreshButtons();
        return;
    }
    m_ui->saveStatusLabel->setText(tr("Saved."));
    m_ui->saveStatusLabel->setStyleSheet("color: #6ee7b7;");
    m_ui->saveStatusLabel->setVisible(true);
    QTimer::singleShot(3000, m_ui->saveStatusLabel, [w = m_ui->saveStatusLabel] { w->setVisible(false); });
    m_saved = true;
    m_assignedScript = parsed.name;
    m_manager->setBotScript(m_botName, parsed.name);

    // Only rebuild the tree when the parser changed something
    // (e.g. added a leading '/' to a command step). Otherwise leave
    // the tree alone so expanded loops and the current selection
    // survive the save.
    const QByteArray beforeJson =
        QJsonDocument(BotScriptParser::serialize(current)).toJson(QJsonDocument::Compact);
    const QByteArray afterJson =
        QJsonDocument(BotScriptParser::serialize(parsed)).toJson(QJsonDocument::Compact);
    if (beforeJson != afterJson)
        rebuildFromScript(parsed);
}

void BotScriptEditor::onRevert()
{
    auto* store = BotScriptStore::instance();
    if (!m_assignedScript.isEmpty() && store->contains(m_assignedScript)) {
        rebuildFromScript(store->script(m_assignedScript));
        m_saved = true;
    } else {
        setBot(m_botName, m_assignedScript, m_connected);
    }
    refreshButtons();
}

void BotScriptEditor::onRun()
{
    if (!m_connected || !m_saved)
        return;
    QString error;
    if (!validate(error))
        return;
    m_manager->runScriptFor(m_botName, currentScript());
}

void BotScriptEditor::onStop()
{
    if (m_botName.isEmpty())
        return;
    m_manager->stopScriptFor(m_botName);
}

void BotScriptEditor::onStoreChanged()
{
    // another window changed the store; reload only when showing saved state
    if (m_saved && !m_assignedScript.isEmpty()) {
        auto* store = BotScriptStore::instance();
        if (store->contains(m_assignedScript))
            rebuildFromScript(store->script(m_assignedScript));
    }
}

void BotScriptEditor::refreshButtons()
{
    QString error;
    const bool valid = validate(error);
    if (!valid) {
        // jump to the offending step so the error lands on its page
        QString stepError;
        const QModelIndex bad = findFirstInvalidStep(QModelIndex(), 0, stepError);
        if (bad.isValid() && bad != m_ui->stepsTree->currentIndex())
            m_ui->stepsTree->setCurrentIndex(bad);
        if (bad.isValid())
            error = stepError;
    }
    // surface the error on the visible property page (re-read: the
    // jump above may have switched it)
    QWidget* page = m_ui->propStack->currentWidget();
    QLabel* target = nullptr;
    if (page == m_ui->pageText)
        target = m_ui->textError;
    else if (page == m_ui->pageWait)
        target = m_ui->waitError;
    else if (page == m_ui->pageChat)
        target = m_ui->chatError;
    else if (page == m_ui->pagePlayer)
        target = m_ui->playerError;
    else if (page == m_ui->pageLoop)
        target = m_ui->loopError;
    if (target)
        target->setText(valid ? QString() : error);
    const bool hasBot = !m_botName.isEmpty();
    m_ui->saveBtn->setEnabled(valid && hasBot);
    m_ui->runBtn->setEnabled(valid && m_saved && m_connected && hasBot && m_ui->enabledCheck->isChecked());
    m_ui->stopBtn->setEnabled(m_connected && hasBot);
    const bool hasSelection = m_ui->stepsTree->currentIndex().isValid();
    m_ui->removeBtn->setEnabled(hasSelection);
    m_ui->upBtn->setEnabled(hasSelection);
    m_ui->downBtn->setEnabled(hasSelection);
}

void BotScriptEditor::setConnected(const QString& botName, bool connected)
{
    if (botName != m_botName)
        return;
    m_connected = connected;
    refreshButtons();
}

QModelIndex BotScriptEditor::findFirstInvalidStep(const QModelIndex& parent, int depth, QString& error) const
{
    auto* model = treeModel(m_ui->stepsTree);
    const int rows = model->rowCount(parent);
    for (int r = 0; r < rows; r++) {
        const QModelIndex idx = model->index(r, 0, parent);
        BotScriptStep* step = model->stepAt(idx);
        if (!step)
            continue;
        if (step->type == "loop" && depth >= BotScriptParser::kMaxLoopDepth) {
            error = "Loops nested deeper than 5 levels are not allowed.";
            return idx;
        }
        if (!BotScriptParser::validateStep(*step, error))
            return idx;
        if (step->type == "loop") {
            const QModelIndex nested = findFirstInvalidStep(idx, depth + 1, error);
            if (nested.isValid())
                return nested;
        }
    }
    return {};
}

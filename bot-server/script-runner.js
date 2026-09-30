// Script runner for launcher-driven bot scripts.
// Single-use state machine: walks steps, fires callbacks, cleans up.
// Created by bot-server/index.js, which maps callbacks onto wire events.
const RUNTIME_CAP_MS = 24 * 60 * 60 * 1000;
const MAX_LOOP_DEPTH = 5;

function createRunner(bot, script, callbacks) {
  const state = {
    stack: [{ steps: script.steps, index: 0, times: 1, iter: 0 }],
    stopped: false,
    finished: false,
    startTime: Date.now(),
    cleanups: new Set(),
  };

  function onCleanup(fn) {
    state.cleanups.add(fn);
  }

  function runCleanups() {
    for (const fn of state.cleanups) {
      try {
        fn();
      } catch {}
    }
    state.cleanups.clear();
  }

  function finish(reason, error) {
    if (state.finished) return;
    state.finished = true;
    runCleanups();
    callbacks.onFinished(reason, error);
  }

  function fail(stepIndex, message) {
    callbacks.onError(stepIndex, message);
    finish('error', message);
  }

  function top() {
    return state.stack[state.stack.length - 1];
  }

  function advance() {
    while (!state.stopped && !state.finished) {
      // Pop all completed frames iteratively.
      while (state.stack.length > 0 && top().index >= top().steps.length) {
        if (state.stack.length === 1) { finish('completed'); return; }
        const child = state.stack.pop();
        child.iter++;
        if (child.times === -1 || child.iter < child.times) {
          child.index = 0;
          state.stack.push(child);
        }
      }
      if (state.stack.length === 0) { finish('completed'); return; }
      if (Date.now() - state.startTime > RUNTIME_CAP_MS) {
        finish('error', 'runtime cap reached');
        return;
      }
      const frame = top();
      const stepIndex = frame.index;
      const step = frame.steps[stepIndex];
      frame.index++;
      if (!runStep(step, stepIndex)) return;  // async, will call advance()
    }
  }

  function runStep(step, stepIndex) {
    if (state.stopped || state.finished) return false;
    if (state.stack.length > MAX_LOOP_DEPTH + 1) {
      fail(stepIndex, 'loop nesting too deep');
      return false;
    }
    try {
      callbacks.onStep(stepIndex, step.type);
      switch (step.type) {
        case 'say':
          bot.chat(step.text);
          return true;
        case 'command': {
          const text = step.text.startsWith('/') ? step.text : '/' + step.text;
          bot.chat(text);
          return true;
        }
        case 'log':
          return true;
        case 'wait': {
          const timer = setTimeout(() => {
            state.cleanups.delete(cleanup);
            advance();
          }, step.seconds * 1000);
          const cleanup = () => clearTimeout(timer);
          onCleanup(cleanup);
          return false;
        }
        case 'wait_for_chat': {
          const pattern = step.case_sensitive ? step.pattern : step.pattern.toLowerCase();
          const onChat = (who, message) => {
            const hay = String(message);
            if (step.case_sensitive ? hay.includes(pattern) : hay.toLowerCase().includes(pattern)) {
              done();
              advance();
            }
          };
          let timer = null;
          const done = () => {
            bot.removeListener('chat', onChat);
            if (timer) clearTimeout(timer);
            state.cleanups.delete(cleanup);
          };
          const cleanup = () => {
            bot.removeListener('chat', onChat);
            if (timer) clearTimeout(timer);
          };
          bot.on('chat', onChat);
          if (step.timeout_seconds > 0) {
            timer = setTimeout(() => {
              done();
              finish('timeout');
            }, step.timeout_seconds * 1000);
          }
          onCleanup(cleanup);
          return false;
        }
        case 'wait_for_player': {
          if (bot.players[step.player]) {
            advance();
            break;
          }
          const onJoin = (player) => {
            const name = typeof player === 'string' ? player : player && player.username;
            if (name === step.player) {
              done();
              advance();
            }
          };
          let timer = null;
          const done = () => {
            bot.removeListener('playerJoined', onJoin);
            if (timer) clearTimeout(timer);
            state.cleanups.delete(cleanup);
          };
          const cleanup = () => {
            bot.removeListener('playerJoined', onJoin);
            if (timer) clearTimeout(timer);
          };
          bot.on('playerJoined', onJoin);
          // timeout_seconds == 0 waits forever: only stop/disconnect ends it
          if (step.timeout_seconds > 0) {
            timer = setTimeout(() => {
              done();
              finish('timeout');
            }, step.timeout_seconds * 1000);
          }
          onCleanup(cleanup);
          return false;
        }
        case 'loop': {
          const frame = { steps: step.steps, index: 0, times: step.times, iter: 0 };
          state.stack.push(frame);
          return true;
        }
        default:
          fail(stepIndex, `unknown step type: ${step.type}`);
          return false;
      }
    } catch (e) {
      fail(stepIndex, e && e.message ? e.message : String(e));
      return false;
    }
  }

  const capTimer = setTimeout(() => {
    finish('error', 'runtime cap reached');
  }, RUNTIME_CAP_MS);
  onCleanup(() => clearTimeout(capTimer));

  function onEnd() {
    if (!state.finished) finish('disconnected');
  }
  bot.once('end', onEnd);
  bot.once('kicked', onEnd);
  onCleanup(() => {
    bot.removeListener('end', onEnd);
    bot.removeListener('kicked', onEnd);
  });

  return {
    start() {
      advance();
    },
    stop() {
      if (state.finished) return;
      state.stopped = true;
      finish('stopped');
    },
    status() {
      if (state.finished || state.stack.length === 0) return null;
      const frame = top();
      // index points past the running step; report the running one
      const at = frame.index > 0 ? frame.index - 1 : 0;
      if (at >= frame.steps.length) return null;
      return { index: at, step_type: frame.steps[at].type };
    },
  };
}

module.exports = { createRunner, RUNTIME_CAP_MS, MAX_LOOP_DEPTH };

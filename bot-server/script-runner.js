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
    if (state.stopped || state.finished) return;
    if (Date.now() - state.startTime > RUNTIME_CAP_MS) {
      finish('error', 'runtime cap reached');
      return;
    }
    while (state.stack.length > 0) {
      const frame = top();
      if (frame.index >= frame.steps.length) {
        if (state.stack.length === 1) {
          finish('completed');
          return;
        }
        state.stack.pop();
        const child = frame;
        child.iter++;
        if (child.times === -1 || child.iter < child.times) {
          child.index = 0;
          state.stack.push(child);
          continue;
        }
        continue;
      }
      const stepIndex = frame.index;
      const step = frame.steps[stepIndex];
      frame.index++;
      runStep(step, stepIndex);
      return;
    }
    finish('completed');
  }

  function runStep(step, stepIndex) {
    if (state.stopped || state.finished) return;
    if (state.stack.length > MAX_LOOP_DEPTH + 1) {
      fail(stepIndex, 'loop nesting too deep');
      return;
    }
    try {
      callbacks.onStep(stepIndex, step.type);
      switch (step.type) {
        case 'say':
          bot.chat(step.text);
          advance();
          break;
        case 'command': {
          const text = step.text.startsWith('/') ? step.text : '/' + step.text;
          bot.chat(text);
          advance();
          break;
        }
        case 'log':
          advance();
          break;
        case 'wait': {
          const timer = setTimeout(() => {
            state.cleanups.delete(cleanup);
            advance();
          }, step.seconds * 1000);
          const cleanup = () => clearTimeout(timer);
          onCleanup(cleanup);
          break;
        }
        case 'wait_for_chat': {
          const pattern = step.case_sensitive ? step.pattern : step.pattern.toLowerCase();
          const onChat = (who, message) => {
            const hay = step.case_sensitive ? message : String(message).toLowerCase();
            if (hay.includes(pattern)) {
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
          break;
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
          if (step.timeout_seconds > 0) {
            timer = setTimeout(() => {
              done();
              finish('timeout');
            }, step.timeout_seconds * 1000);
          } else if (step.timeout_seconds === 0) {
            // wait forever: only stop/disconnect ends this step
          }
          onCleanup(cleanup);
          break;
        }
        case 'loop': {
          const frame = { steps: step.steps, index: 0, times: step.times, iter: 0 };
          state.stack.push(frame);
          advance();
          break;
        }
        default:
          fail(stepIndex, `unknown step type: ${step.type}`);
          break;
      }
    } catch (e) {
      fail(stepIndex, e && e.message ? e.message : String(e));
    }
  }

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

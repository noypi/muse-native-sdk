'use strict';
// T2 — subsystem observer fan-out for every applied transition.
// Wraps the T1 machine; fans out only when dispatch returns applied:true.

function createObserverHub(machine) {
  const subscribers = [];
  const errors = [];
  let seq = 0;

  function subscribe(fn) {
    subscribers.push(fn);
    let active = true;
    return function unsubscribe() {
      if (!active) {
        return;
      }
      active = false;
      const index = subscribers.indexOf(fn);
      if (index !== -1) {
        subscribers.splice(index, 1);
      }
    };
  }

  function dispatch(transition) {
    const result = machine.dispatch(transition);
    if (!result || result.applied !== true) {
      return result;
    }
    seq += 1;
    const event = { seq, transition, state: result.state };
    const snapshot = subscribers.slice();
    for (const fn of snapshot) {
      try {
        fn(event);
      } catch (err) {
        errors.push(err);
      }
    }
    return { applied: result.applied, state: result.state };
  }

  return { subscribe, dispatch, errors };
}

module.exports = { createObserverHub };

// Minimal react/@muse/native stubs for acceptance execution of muse-demo
// pure-logic regions (hooks only run inside components, which we never render).
export const useState = (v) => [v, () => {}];
export const useEffect = () => {};
export const useMemo = (f) => f();
export const useCallback = (f) => f;
export const useRef = (v) => ({ current: v });
export const View = 'View';
export const Text = 'Text';
export const SnackbarHost = 'SnackbarHost';
export default { useState, useEffect, useMemo, useCallback, useRef };

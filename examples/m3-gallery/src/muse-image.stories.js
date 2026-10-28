'use strict';
// T8 gallery story file — MuseImage all five states in light+dark (T8 owns it).
// Proves AC-MUSE-030 (every state has a light+dark story; moderated renders M3
// illustration + message + edit affordance, never a broken-image icon) and
// documents the TC-MUSE-030 manual half in-file (not executed in CI).
//
// TC-MUSE-030 manual procedure:
// 1. Render each story below on a device (light, then dark theme).
// 2. Confirm `idle` shows the empty slot, `generating` shows the progressive
//    shimmer in the reserved box, `ready` shows the generated asset at the
//    requested size, `moderated` shows the M3 blocked illustration + message +
//    edit affordance (never a broken-image icon), `error` shows the named
//    error (never a silent empty).
// 3. Confirm the progressive preview never shifts layout (fixed aspect box
//    from `size`), then cross-fades to the final asset.
// 4. Toggle airplane mode and confirm the offline named error state.
//
// Hermetic: pure descriptor objects only; no imports, no network.

function imageStory(status, theme, extra) {
  return Object.assign(
    {
      component: 'MuseImage',
      status,
      theme,
      style: 'avatar',
      size: '512x512',
      contentDescription: 'Avatar for Ada, engineer. Loves robots.',
    },
    extra
  );
}

const READY_ASSET = { url: 'generated:seed-abc-123', seed: 'seed-abc-123', width: 512, height: 512 };

const stories = {
  idle: {
    light: imageStory('idle', 'light', { url: null }),
    dark: imageStory('idle', 'dark', { url: null }),
  },
  generating: {
    light: imageStory('generating', 'light', { previewUrl: 'preview:1', reservedBox: { width: 512, height: 512 } }),
    dark: imageStory('generating', 'dark', { previewUrl: 'preview:1', reservedBox: { width: 512, height: 512 } }),
  },
  ready: {
    light: imageStory('ready', 'light', READY_ASSET),
    dark: imageStory('ready', 'dark', READY_ASSET),
  },
  moderated: {
    light: imageStory('moderated', 'light', {
      illustration: 'm3-blocked',
      message: 'This prompt was blocked by safety review. Edit the description and try again.',
      editAffordance: true,
    }),
    dark: imageStory('moderated', 'dark', {
      illustration: 'm3-blocked',
      message: 'This prompt was blocked by safety review. Edit the description and try again.',
      editAffordance: true,
    }),
  },
  error: {
    light: imageStory('error', 'light', { code: 'timeout', route: 'generate-image' }),
    dark: imageStory('error', 'dark', { code: 'timeout', route: 'generate-image' }),
  },
};

const STATUSES = ['idle', 'generating', 'ready', 'moderated', 'error'];
const THEMES = ['light', 'dark'];

module.exports = { stories, STATUSES, THEMES };

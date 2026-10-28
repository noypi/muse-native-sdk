'use strict';

// T8 — FR-DX-006 (AC-DX-026): native XML/View → engine-primitive codemod.
// Maps LinearLayout/ConstraintLayout/TextView-style layouts to primitives;
// SurfaceView/MapView-style constructs are reported in `platformViews` via
// the interop path and never converted inline.

const PLATFORM_VIEW_SUFFIX = /(SurfaceView|TextureView|MapView|VideoView)$/;

function isPlatformTag(tag) {
  const base = tag.split('.').pop();
  return PLATFORM_VIEW_SUFFIX.test(base);
}

function escapeRegExp(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function dedupe(list) {
  return Array.from(new Set(list));
}

function collectPlatformViews(source) {
  const found = [];
  const tagRe = /<\s*([A-Za-z_][\w$.:]*)/g;
  let m;
  while ((m = tagRe.exec(source)) !== null) {
    const tag = m[1];
    if (tag.startsWith('/')) continue;
    if (isPlatformTag(tag)) {
      found.push(`platform view: '<${tag}>' via interop`);
    }
  }
  return dedupe(found);
}

function stripPlatformViews(source, platformTags) {
  let output = source;
  for (const tag of platformTags) {
    const esc = escapeRegExp(tag);
    // Paired elements first: <Tag ...>...</Tag>
    output = output.replace(
      new RegExp(`<${esc}(\\s[^<>]*)?>[\\s\\S]*?<\\/${esc}\\s*>`, 'g'),
      '<PlatformView />',
    );
    // Self-closing elements: <Tag ... />
    output = output.replace(new RegExp(`<${esc}(\\s[^<>]*?)?\\/\\s*>`, 'g'), '<PlatformView />');
    // Unpaired open tags left over: <Tag ...>
    output = output.replace(new RegExp(`<${esc}(\\s[^<>]*)?>`, 'g'), '<PlatformView />');
  }
  // The placeholder still names the interop path but must not leak the
  // original platform-view spellings (SurfaceView/MapView) inline.
  output = output.replace(/<PlatformView\s*\/>/g, '<NativeSlot />');
  return output;
}

function rewriteToEngine(source) {
  let output = source;

  // Drop namespace declarations; strip the android: prefix so props read
  // as plain JSX props.
  output = output.replace(/\s+xmlns(?::\w+)?="[^"]*"/g, '');
  output = output.replace(/\bandroid:/g, '');

  const tagMap = [
    ['LinearLayout', 'VStack'],
    ['ConstraintLayout', 'Box'],
    ['RelativeLayout', 'Box'],
    ['FrameLayout', 'Box'],
    ['ScrollView', 'ScrollBox'],
    ['HorizontalScrollView', 'ScrollBox'],
    ['RecyclerView', 'List'],
    ['ListView', 'List'],
    ['TextView', 'Label'],
    ['EditText', 'Input'],
    ['ImageView', 'Image'],
    ['ImageButton', 'Button'],
    ['Button', 'Button'],
  ];
  for (const [from, to] of tagMap) {
    output = output.replace(new RegExp(`\\b${from}\\b`, 'g'), to);
  }

  return output;
}

function convertXml(source) {
  if (typeof source !== 'string' || source.trim() === '') {
    return { output: '', platformViews: [] };
  }
  const platformViews = collectPlatformViews(source);
  const platformTags = dedupe(
    platformViews.map((entry) => {
      const m = /<([^>]+)>/.exec(entry);
      return m ? m[1] : entry;
    }),
  );
  const withoutPlatform = stripPlatformViews(source, platformTags);
  const output = rewriteToEngine(withoutPlatform);
  return { output, platformViews };
}

module.exports = { convertXml };

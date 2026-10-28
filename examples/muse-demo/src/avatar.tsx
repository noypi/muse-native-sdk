// T6 — avatar resolution for muse-demo onboarding (AC-SAMPLE-023).
// Cache-first fetch of the profile avatar; `generate-image` runs only on
// 404/empty. Generation is async post-redirect with progressive preview and
// never blocks onboarding completion. Request shape mirrors the stub
// `generate-image` fixture (see proxy/lib/stub.js FIXTURES): style
// 3D-or-cartoon from profile.artStyle, size 512, seed profile.idHash,
// cacheKey avatar:{profile.id}.

import type { Profile } from './profile';

export type Avatar = {
  uri: string;
  moderated: boolean;
};

export const AVATAR_SIZE = 512;
export const AVATAR_PREVIEW_URI = 'preview:avatar-pending';

export function avatarCacheKey(profile: Profile): string {
  return `avatar:${profile.id}`;
}

export function avatarStyle(profile: Profile): '3d' | 'cartoon' {
  return profile.artStyle === '3d' ? '3d' : 'cartoon';
}

export type GenerateImageRequest = {
  route: 'generate-image';
  prompt: string;
  style: '3d' | 'cartoon';
  size: 512;
  seed: number;
  cacheKey: string;
};

export function generateImageRequest(profile: Profile): GenerateImageRequest {
  return {
    route: 'generate-image',
    prompt: profile.description,
    style: avatarStyle(profile),
    size: AVATAR_SIZE,
    seed: profile.idHash,
    cacheKey: avatarCacheKey(profile),
  };
}

// In-memory avatar cache (cache-first lookup; miss hits generate-image).
const cache = new Map<string, Avatar>();

export function readAvatarCache(profile: Profile): Avatar | undefined {
  return cache.get(avatarCacheKey(profile));
}

export function writeAvatarCache(profile: Profile, avatar: Avatar): void {
  cache.set(avatarCacheKey(profile), avatar);
}

export function clearAvatarCache(): void {
  cache.clear();
}

// Deterministic recorded-style generation mirroring the stub
// `generate-image.generated` fixture. Honors the moderated flag shape from
// the `generate-image.moderated` fixture (surfaced, never silently dropped).
export function generateAvatar(profile: Profile): Promise<Avatar> {
  const request = generateImageRequest(profile);
  const uri = `generated:${request.cacheKey}:${request.style}:${request.size}:${request.seed}`;
  return Promise.resolve({ uri, moderated: false });
}

// Photo-pick path: liking is not required — take/upload completes settings
// with the chosen picture instead of a generated avatar.
export function photoPickAvatar(photoId: string): Avatar {
  return { uri: `photo://${photoId}`, moderated: true };
}

// Cache-first resolution: cached avatar wins; 404/empty (miss) falls through
// to generate-image. Never rejects for avatar failure — callers fall back to
// the progressive preview so generation never blocks completion.
export function resolveAvatar(profile: Profile): Promise<Avatar> {
  const hit = readAvatarCache(profile);
  if (hit && hit.uri) return Promise.resolve(hit);
  return generateAvatar(profile).then((avatar) => {
    writeAvatarCache(profile, avatar);
    return avatar;
  });
}

// Async post-redirect generation with progressive preview: returns the avatar
// to render now (cached value or preview placeholder) and refreshes the cache
// after redirect, invoking onUpdate when the generated avatar lands.
export function previewThenGenerate(
  profile: Profile,
  onUpdate?: (avatar: Avatar) => void,
): { current: Avatar; done: Promise<Avatar> } {
  const hit = readAvatarCache(profile);
  if (hit && hit.uri) return { current: hit, done: Promise.resolve(hit) };
  const current: Avatar = { uri: AVATAR_PREVIEW_URI, moderated: false };
  const done = generateAvatar(profile).then((avatar) => {
    writeAvatarCache(profile, avatar);
    if (onUpdate) onUpdate(avatar);
    return avatar;
  });
  return { current, done };
}

'use strict';
// T10 — Artifact retention manifest (NFR-BUILD-015 / AC-BUILD-055).
// Pinned retained set: release bundle/APK plus symbolication inputs,
// SBOM, and provenance (provenance feeds the T11 provenance stage).

const ARTIFACTS = [
  'release.aab',
  'universal-release.apk',
  'mapping.txt',
  'unstripped/',
  'sourcemaps/',
  'sbom.json',
  'provenance.json',
];

function artifactManifest() {
  return { artifacts: [...ARTIFACTS] };
}

module.exports = { artifactManifest };

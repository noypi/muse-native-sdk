'use strict';
// @muse/intelligence root entry: re-export the public surface so consumers
// can `require('@muse/intelligence')` instead of deep-importing `lib/*`.

const { createTurn } = require('./lib/turn.js');
const { assertMuseRoutes } = require('./lib/routes.js');
const {
  createMuseVoice,
  MuseVoice,
  releaseTarget,
  startTurn,
  openListeningSheet,
  commitSheet,
  MuseVoiceSpeaker,
} = require('./lib/voice.js');
const { MuseImage } = require('./lib/image.js');
const { createPlatformSpeech } = require('./lib/platform-speech.js');
const { selectRow, retainSelection } = require('./lib/list.js');

module.exports = {
  createTurn,
  assertMuseRoutes,
  createMuseVoice,
  MuseVoice,
  releaseTarget,
  startTurn,
  openListeningSheet,
  commitSheet,
  MuseVoiceSpeaker,
  MuseImage,
  createPlatformSpeech,
  selectRow,
  retainSelection,
};

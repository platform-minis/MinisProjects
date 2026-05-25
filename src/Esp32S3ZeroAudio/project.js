// project.js — Esp32S3ZeroAudio
// MAX98357A I2S amplifier: BCLK=GP4, LRCLK=GP5, DIN=GP6

Blockly.defineBlocksWithJsonArray([
  {
    type: 'audio_init',
    message0: 'MAX98357A init  BCLK=GP4  LRCLK=GP5  DIN=GP6',
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Initialise MAX98357A I2S amplifier on BCLK=GP4, LRCLK=GP5, DIN=GP6. Call once in setup.',
  },
  {
    type: 'audio_tone',
    message0: 'play tone  freq %1 Hz  for %2 ms',
    args0: [
      { type: 'input_value', name: 'FREQ', check: 'Number' },
      { type: 'input_value', name: 'MS',   check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Play a sine-wave tone at the given frequency (Hz) for the given duration (ms).',
  },
  {
    type: 'audio_note',
    message0: 'play note %1  for %2 ms',
    args0: [
      {
        type: 'field_dropdown',
        name: 'NOTE',
        options: [
          ['C4 (262 Hz)', 'C4'],
          ['D4 (294 Hz)', 'D4'],
          ['E4 (330 Hz)', 'E4'],
          ['F4 (349 Hz)', 'F4'],
          ['G4 (392 Hz)', 'G4'],
          ['A4 (440 Hz)', 'A4'],
          ['B4 (494 Hz)', 'B4'],
          ['C5 (523 Hz)', 'C5'],
        ],
      },
      { type: 'input_value', name: 'MS', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Play a musical note for the given duration (ms).',
  },
  {
    type: 'audio_rest',
    message0: 'rest for %1 ms',
    args0: [
      { type: 'input_value', name: 'MS', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 30,
    tooltip: 'Output silence for the given duration (ms).',
  },
]);

var _I2S_AUDIO_DEFS = `
from machine import Pin, I2S
import math, struct
_RATE = 22050
_NOTES = {'C4':262,'D4':294,'E4':330,'F4':349,'G4':392,'A4':440,'B4':494,'C5':523}
_i2s = None
def _audio_init():
    global _i2s
    _i2s = I2S(0, sck=Pin(4), ws=Pin(5), sd=Pin(6), mode=I2S.TX, bits=16, format=I2S.MONO, rate=_RATE, ibuf=4096)
def _play(freq, ms, vol=0.5):
    if _i2s is None:
        return
    if freq <= 0:
        _i2s.write(bytearray(int(_RATE * ms / 1000) * 2))
        return
    period = int(_RATE / freq)
    one = bytearray(period * 2)
    amp = int(32767 * vol)
    for i in range(period):
        struct.pack_into('<h', one, i * 2, int(amp * math.sin(6.2832 * i / period)))
    total = int(_RATE * ms / 1000)
    written = 0
    while written < total:
        chunk = min(period, total - written)
        _i2s.write(one[:chunk * 2])
        written += chunk
def _play_note(name, ms):
    _play(_NOTES.get(name, 0), ms)
`;

generator.forBlock['audio_init'] = function (_block, g) {
  g.addImport('i2s_audio_defs', _I2S_AUDIO_DEFS);
  return '_audio_init()\n';
};

generator.forBlock['audio_tone'] = function (block, g) {
  g.addImport('i2s_audio_defs', _I2S_AUDIO_DEFS);
  var freq = g.valueToCode(block, 'FREQ', Order.NONE) || '440';
  var ms   = g.valueToCode(block, 'MS',   Order.NONE) || '500';
  return '_play(' + freq + ', ' + ms + ')\n';
};

generator.forBlock['audio_note'] = function (block, g) {
  g.addImport('i2s_audio_defs', _I2S_AUDIO_DEFS);
  var note = block.getFieldValue('NOTE');
  var ms   = g.valueToCode(block, 'MS', Order.NONE) || '400';
  return '_play_note(\'' + note + '\', ' + ms + ')\n';
};

generator.forBlock['audio_rest'] = function (block, g) {
  g.addImport('i2s_audio_defs', _I2S_AUDIO_DEFS);
  var ms = g.valueToCode(block, 'MS', Order.NONE) || '200';
  return '_play(0, ' + ms + ')\n';
};

addCategory({
  name: 'MAX98357A Audio',
  colour: '#e65100',
  blocks: ['audio_init', 'audio_tone', 'audio_note', 'audio_rest'],
});

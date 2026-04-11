// project.js — custom Blockly blocks for Esp32S3uPythonCurses
// Available variables: Blockly, generator, addCategory({ name, colour, blocks }), Order, addLibrary({ url, remoteName })

var RAW = 'https://raw.githubusercontent.com/platform-minis/MinisProjects/main/libs/uMinisLib/';
addLibrary({ url: RAW + 'minis_iot.py',     remoteName: 'minis_iot.py' });
addLibrary({ url: RAW + 'minis_display.py', remoteName: 'minis_display.py' });

Blockly.defineBlocksWithJsonArray([
  {
    type: 'curses_init',
    message0: 'curses init',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Initialize the curses library and create the standard screen.',
  },
  {
    type: 'curses_deinit',
    message0: 'curses deinit',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Restore the terminal to its original state.',
  },
  {
    type: 'curses_clear',
    message0: 'curses clear',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Clear the screen.',
  },
  {
    type: 'curses_refresh',
    message0: 'curses refresh',
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Refresh the screen to show pending changes.',
  },
  {
    type: 'curses_print',
    message0: 'curses print at row %1 col %2 text %3',
    args0: [
      { type: 'input_value', name: 'ROW', check: 'Number' },
      { type: 'input_value', name: 'COL', check: 'Number' },
      { type: 'input_value', name: 'TEXT', check: 'String' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Print text at the given row and column.',
  },
  {
    type: 'curses_getkey',
    message0: 'curses get key',
    output: 'String',
    colour: 150,
    tooltip: 'Wait for and return a keypress.',
  },
  {
    type: 'curses_move',
    message0: 'curses move to row %1 col %2',
    args0: [
      { type: 'input_value', name: 'ROW', check: 'Number' },
      { type: 'input_value', name: 'COL', check: 'Number' },
    ],
    inputsInline: true,
    previousStatement: null,
    nextStatement: null,
    colour: 150,
    tooltip: 'Move the cursor to the given position.',
  },
]);

generator.forBlock['curses_init'] = function (_block, g) {
  g.addImport('curses', 'import curses');
  return 'stdscr = curses.initscr()\n';
};

generator.forBlock['curses_deinit'] = function (_block, _g) {
  return 'curses.endwin()\n';
};

generator.forBlock['curses_clear'] = function (_block, _g) {
  return 'stdscr.clear()\n';
};

generator.forBlock['curses_refresh'] = function (_block, _g) {
  return 'stdscr.refresh()\n';
};

generator.forBlock['curses_print'] = function (block, g) {
  var row = g.valueToCode(block, 'ROW', Order.NONE) || '0';
  var col = g.valueToCode(block, 'COL', Order.NONE) || '0';
  var text = g.valueToCode(block, 'TEXT', Order.NONE) || '""';
  return 'stdscr.addstr(' + row + ', ' + col + ', str(' + text + '))\n';
};

generator.forBlock['curses_getkey'] = function (_block, _g) {
  return ['stdscr.getkey()', Order.ATOMIC];
};

generator.forBlock['curses_move'] = function (block, g) {
  var row = g.valueToCode(block, 'ROW', Order.NONE) || '0';
  var col = g.valueToCode(block, 'COL', Order.NONE) || '0';
  return 'stdscr.move(' + row + ', ' + col + ')\n';
};

addCategory({
  name: 'Curses',
  colour: '#2e8b57',
  blocks: [
    'curses_init',
    'curses_deinit',
    'curses_clear',
    'curses_refresh',
    'curses_print',
    'curses_getkey',
    'curses_move',
  ],
});

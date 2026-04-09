// project.js — custom Blockly blocks for Esp32S3uPythonCurses
// Available variables: Blockly, generator, addCategory({ name, colour, blocks })

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

generator.forBlock['curses_init'] = function (_block, gen) {
  gen.addImport('curses', 'import curses');
  gen.addDeclaration('curses_stdscr', 'stdscr = curses.initscr()');
  return '';
};

generator.forBlock['curses_deinit'] = function (_block, _gen) {
  return 'curses.endwin()\n';
};

generator.forBlock['curses_clear'] = function (_block, _gen) {
  return 'stdscr.clear()\n';
};

generator.forBlock['curses_refresh'] = function (_block, _gen) {
  return 'stdscr.refresh()\n';
};

generator.forBlock['curses_print'] = function (block, gen) {
  const row = gen.valueToCode(block, 'ROW', gen.ORDER_ATOMIC) || '0';
  const col = gen.valueToCode(block, 'COL', gen.ORDER_ATOMIC) || '0';
  const text = gen.valueToCode(block, 'TEXT', gen.ORDER_ATOMIC) || '""';
  return `stdscr.addstr(${row}, ${col}, str(${text}))\n`;
};

generator.forBlock['curses_getkey'] = function (_block, _gen) {
  return ['stdscr.getkey()', gen.ORDER_FUNCTION_CALL];
};

generator.forBlock['curses_move'] = function (block, gen) {
  const row = gen.valueToCode(block, 'ROW', gen.ORDER_ATOMIC) || '0';
  const col = gen.valueToCode(block, 'COL', gen.ORDER_ATOMIC) || '0';
  return `stdscr.move(${row}, ${col})\n`;
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

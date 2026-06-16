// project.js — libraries for CYDTest
// micro-gui from https://github.com/peterhinch/micropython-micro-gui
// Available variables: Blockly, generator, addCategory({ name, colour, blocks }), Order, addLibrary({ url, remoteName })

var MG = 'https://raw.githubusercontent.com/peterhinch/micropython-micro-gui/master/';

addLibrary({ url: MG + 'drivers/display/ili9341.py', remoteName: 'drivers/display/ili9341.py' });
addLibrary({ url: MG + 'gui/core/nanogui.py',        remoteName: 'gui/core/nanogui.py' });
addLibrary({ url: MG + 'gui/core/writer.py',          remoteName: 'gui/core/writer.py' });
addLibrary({ url: MG + 'gui/widgets/label.py',        remoteName: 'gui/widgets/label.py' });
addLibrary({ url: MG + 'gui/widgets/button.py',       remoteName: 'gui/widgets/button.py' });
addLibrary({ url: MG + 'gui/fonts/freesans20.py',     remoteName: 'gui/fonts/freesans20.py' });

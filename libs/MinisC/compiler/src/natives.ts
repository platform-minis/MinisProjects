import { CType } from './ast';

export interface NativeInfo {
    name:    string;
    argc:    number;
    retType: CType;
}

// Must match registerNative() calls in the Arduino sketch.
// Order = native index passed to OP_CALL_NATIVE.
export const NATIVES: NativeInfo[] = [
    { name: 'gpio_write',   argc: 2, retType: 'void'  },  // 0
    { name: 'gpio_read',    argc: 1, retType: 'int'   },  // 1
    { name: 'analog_read',  argc: 1, retType: 'int'   },  // 2
    { name: 'delay',        argc: 1, retType: 'void'  },  // 3
    { name: 'millis',       argc: 0, retType: 'int'   },  // 4
    { name: 'print_int',    argc: 1, retType: 'void'  },  // 5
    { name: 'print_float',  argc: 1, retType: 'void'  },  // 6
    // math
    { name: 'abs_i',        argc: 1, retType: 'int'   },  // 7
    { name: 'abs_f',        argc: 1, retType: 'float' },  // 8
    { name: 'min_i',        argc: 2, retType: 'int'   },  // 9
    { name: 'max_i',        argc: 2, retType: 'int'   },  // 10
    { name: 'map_i',        argc: 5, retType: 'int'   },  // 11  map(val,inMin,inMax,outMin,outMax)
    { name: 'constrain',    argc: 3, retType: 'int'   },  // 12  constrain(val,lo,hi)
    { name: 'random',       argc: 2, retType: 'int'   },  // 13  random(lo, hi)
    // mqtt
    { name: 'mqtt_pub_int', argc: 2, retType: 'void'  },  // 14  topic(str), value(int)
    { name: 'mqtt_pub_flt', argc: 2, retType: 'void'  },  // 15  topic(str), value(float)
    { name: 'mqtt_pub_str', argc: 2, retType: 'void'  },  // 16  topic(str), value(str)
];

export function buildNativeMap(): Map<string, { idx: number; info: NativeInfo }> {
    const m = new Map<string, { idx: number; info: NativeInfo }>();
    NATIVES.forEach((n, i) => m.set(n.name, { idx: i, info: n }));
    return m;
}

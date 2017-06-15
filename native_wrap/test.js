'use strict';
const wrap = require('./build/Release/native_wrap');
//const wrap = require('./build/Debug/native_wrap');
const assert = require('assert');

var obj = wrap.createObject(10);

assert.strictEqual(obj.plusOne(), 11);
assert.strictEqual(obj.plusOne(),12);

assert.strictEqual(wrap.plusOne(obj), 13);

obj = null;

//global.gc();

assert.throws(
  () => {
    var not_obj = { value: 4 }
    wrap.plusOne(not_obj)
  },
  /.*/
);

assert.throws(
  () => {
    wrap.plusOne(null)
  },
  /.*/
);

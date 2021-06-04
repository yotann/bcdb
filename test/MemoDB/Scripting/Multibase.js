// RUN: memodb js %s

function equals(x, y) {
  if (x instanceof Uint8Array && y instanceof Uint8Array) {
    return Duktape.enc('jx', x) == Duktape.enc('jx', y);
  } else {
    return x == y;
  }
}

function checkEquals(x, y) {
  "use strict";
  if (!(equals(x, y)))
    throw new Error("check failure: " + Duktape.enc('jx', x) + " != " + Duktape.enc('jx', y));
}

checkEquals(Multibase.base32.prefix, "b");
checkEquals(Multibase.base32.name, "base32");
checkEquals(Multibase.base32.encode(new Uint8Array([0x55, 0xaa])), "bkwva");
checkEquals(Multibase.base32.encodeWithoutPrefix(new Uint8Array([0x55, 0xaa])), "kwva");
checkEquals(Multibase.decode("bkwva"), new Uint8Array([0x55, 0xaa]));
checkEquals(Multibase.base32.decodeWithoutPrefix("kwva"), new Uint8Array([0x55, 0xaa]));

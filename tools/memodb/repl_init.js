// Based on:
// - duktape/examples/cmdline/duk_cmdline.c
// - duktape/extras/console/duk_console.c
// - duktape/polyfills/console-minimal.js

(function(global_stash) {
  // Save original functions in case they're overwritten.
  var origEnc = Duktape.enc;
  var origPrint = print;
  var origAlert = alert;

  // Helper functions

  function eachProperty(obj, func) {
    var sanity = 1000;
    while (obj != null) {
      if (--sanity < 0) return;
      var names = Object.getOwnPropertyNames(Object(obj));
      for (var i = 0; i < names.length; i++) {
        if (--sanity < 0) return;
        func(obj, names[i]);
      }
      obj = Object.getPrototypeOf(Object(obj));
    }
  }

  function format(v) {
    try {
      return origEnc('jx', v);
    } catch(e) {
      return String(v);
    }
  };

  function consoleHelper(args, print, error_name) {
    var strArgs = Array.prototype.map.call(args, format);
    var msg = Array.prototype.join.call(strArgs, ' ');
    if (error_name) {
      var err = new Error(msg);
      err.name = error_name;
      print(err.stack);
    } else {
      print(msg);
    }
  }

  global_stash.dukFormat = format;

  // https://console.spec.whatwg.org/

  this.console = {};
  this.console.log = function() { consoleHelper(arguments, origPrint); }
  this.console.debug = this.console.log;
  this.console.info = this.console.log;
  this.console.trace = function() { consoleHelper(arguments, origPrint, 'Trace'); }
  this.console.warn = function() { consoleHelper(arguments, origAlert); }
  this.console.error = function() { consoleHelper(arguments, origAlert, 'Error'); }
  this.console.exception = this.console.error;
  this.console.assert = function(cond) {
    if (!cond)
      consoleHelper(Array.prototype.slice.call(arguments, 1), origPrint, 'AssertionError');
  };

  this.console.dir = function(item) {
    eachProperty(item, function(obj, name) {
      origPrint(name + ': ' + format(obj[name]));
    });
  };

  var countMap = {};
  this.console.count = function(label) {
    if (label in countMap)
      countMap[label]++;
    else
      countMap[label] = 1;
    consoleHelper([label + ': ' + countMap[label].toString()], origPrint);
  };
  this.console.countReset = function(label) {
    if (label in countMap)
      countMap[label] = 0;
    else
      consoleHelper([label + ' does not have a count'], origPrint);
  };

  // linenoise completion helpers

  function followProperties(input) {
    // Find maximal trailing string which looks like a property access. Look up
    // all the components (starting from the global object now) except the
    // last; treat the last component as a partial name.
    var match, propseq, obj, i, partial;

    match = /^.*?((?:\w+\.)*\w*)$/.exec(input);
    var propseq = match[1].split('.');

    obj = Function('return this')();
    for (i = 0; i < propseq.length - 1; i++) {
      if (obj === void 0 || obj === null) { return; }
      obj = obj[propseq[i]];
    }
    if (obj === void 0 || obj === null) { return; }

    return [obj, propseq[propseq.length - 1]];
  }

  function linenoiseCompletion(input, addCompletion, arg) {
    var followed = followProperties(input);
    if (!followed)
      return;
    var obj = followed[0];
    var partial = followed[1];
    eachProperty(obj, function(obj, name) {
      if (Number(name) >= 0) return; // ignore array keys
      if (name.substring(0, partial.length) !== partial) return;
      if (name === partial) { addCompletion(input + '.', arg); return; }
      addCompletion(input + name.substring(partial.length), arg);
    });
  }

  function linenoiseHints(input) {
    var followed = followProperties(input);
    if (!followed)
      return;
    var obj = followed[0];
    var partial = followed[1];
    var result = [];
    var lastObj;
    var found = Object.create(null);
    eachProperty(obj, function(obj, name) {
      if (Number(name) >= 0) return; // ignore array keys
      if (name.substring(0, partial.length) !== partial) return;
      if (name === partial) return;
      if (found[name]) return;
      found[name] = true;
      var first = obj !== lastObj;
      lastObj = obj;
      result.push(result.length === 0 ? name.substring(partial.length) : (first ? ' || ' : ' | ') + name);
    });
    return { hints: result.join(''), color: 35, bold: 1 };
  }

  global_stash.linenoiseCompletion = linenoiseCompletion;
  global_stash.linenoiseHints = linenoiseHints;
})

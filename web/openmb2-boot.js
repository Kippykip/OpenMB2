// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 the OpenMB2 contributors
//
// Sets up the page for openmb2.js: the canvas, a loading bar, and a
// "click to play" cover. Browsers only allow sound and a captured mouse
// after a click, so the game starts on the first one.
//
// The other files are looked for next to this script, so the page itself
// can be somewhere else.
(function () {
    var base = document.currentScript.src.replace(/[^\/]*$/, '');
    var cover = document.getElementById('openmb2-start');
    var status = document.getElementById('openmb2-status');
    var bar = document.querySelector('#openmb2-bar div');
    var canvas = document.getElementById('canvas');
    var started = false;

    canvas.addEventListener('webglcontextlost', function (e) {
        status.textContent = 'The graphics stopped working. Reload the page to play again.';
        cover.hidden = false;
        e.preventDefault();
    });

    function start() {
        if (started || !window.Module.openmb2Ready)
            return;
        started = true;
        cover.hidden = true;
        canvas.focus();
        window.Module.callMain(window.Module.arguments);
    }
    cover.addEventListener('click', start);

    window.Module = {
        canvas: canvas,
        locateFile: function (path) { return base + path; },
        arguments: ['--game', '/game'],
        noInitialRun: true,
        openmb2Ready: false,
        print: function (text) { console.log(text); },
        printErr: function (text) { console.warn(text); },
        setStatus: function (text) {
            var m = /\((\d+(?:\.\d+)?)\/(\d+)\)/.exec(text);
            if (m)
                bar.style.width = (100 * m[1] / m[2]) + '%';
            if (text && !started)
                status.textContent = text.replace(/\s*\(.*\)$/, '…');
        },
        onRuntimeInitialized: function () {
            window.Module.openmb2Ready = true;
            bar.style.width = '100%';
            status.textContent = 'Click to play';
        },
        onAbort: function (what) {
            status.textContent = 'OpenMB2 stopped: ' + what;
            cover.hidden = false;
        }
    };
})();

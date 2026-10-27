// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 the OpenMB2 contributors
//
// Runs before the game starts in a browser: /save, where the game keeps
// saved games, scores and openmb2.ini, is loaded from IndexedDB first.
// src/userfile.c copies it back after every write.
Module.preRun = Module.preRun || [];
Module.preRun.push(function () {
    FS.mkdir('/save');
    FS.mount(IDBFS, {}, '/save');
    addRunDependency('openmb2-save');
    FS.syncfs(true, function (err) {
        if (err)
            console.warn('OpenMB2: saved files could not be loaded', err);
        removeRunDependency('openmb2-save');
    });
});

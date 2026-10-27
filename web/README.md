# OpenMB2 in a web browser

OpenMB2 builds for browsers with [Emscripten](https://emscripten.org)
(WebGL 2, or WebGL 1 where there is no WebGL 2). The page downloads the
game data packed into one file, so you host your own copy of the game
next to it.

## Build

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DOPENMB2_BUILD_TOOLS=OFF
cmake --build build-web
```

This makes `build-web/openmb2.js` and `build-web/openmb2.wasm`.

## Pack the game data

Put the game folder (with the `OpenMB2` folder from `data/` copied into
it) in a folder called `game`, then, from the folder that holds it:

```sh
python3 $EMSDK/upstream/emscripten/tools/file_packager.py openmb2.data \
    --preload game@/game --use-preload-cache --js-output=openmb2-data.js
```

`--use-preload-cache` keeps the data in the browser, so it is only
downloaded once.

## Put it on a page

Copy these next to each other on the web server:

- `index.html` and `openmb2-boot.js` from this folder
- `openmb2.js` and `openmb2.wasm` from the build
- `openmb2-data.js` and `openmb2.data` from the packer

`index.html` is only an example: any page with the same `canvas`,
`openmb2-start` cover and three scripts works, and the scripts may be in
another folder than the page. The game starts on the first click, since
browsers only allow sound and a captured mouse after one.

Saved games, high scores and `openmb2.ini` are kept in the browser's
IndexedDB (`/save` in the game, see `pre.js`). Quit in the main menu does
nothing in a browser, and full screen is only entered with Alt+Enter.

Online high scores don't work in a browser yet.

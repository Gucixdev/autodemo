## Sizes (all lossless WebP unless noted)

| demo | size | length |
|---|---|---|
| .autodemo/examples/ls.demo (80x16) | ~46 KiB | 8.9 s |
| .autodemo/examples/frost.demo (100x28) | ~22 KiB | 5.6 s |
| .autodemo/examples/lazytui.demo (110x34, constantly animating UI) | ~550 KiB (webm VP9: ~290 KiB) | 10.5 s |

Static-ish terminals compress to tiny files because only changed regions of changed frames are stored;
a UI that animates every frame is the worst case.

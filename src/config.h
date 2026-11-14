/* config.h - autodemo defaults. Change them here and rebuild; a demo script overrides most with a header directive. */
#ifndef AUTODEMO_CONFIG_H
#define AUTODEMO_CONFIG_H

#define CFG_TRANSITION     "fade"  /* default effect between a slide and the terminal (see fx.c for the names) */
#define CFG_TRANSITION_MS  300     /* its length */
#define CFG_FX_STEP_MS     40      /* one effect frame (25 fps); smaller = smoother and a bigger file */
#define CFG_SLIDE_MS       2000    /* how long a slide stays when the script gives no time */
#define CFG_ZOOM_AMOUNT    0.25    /* zoom effects: how far the picture scales (0.25 = 25 %) */
#define CFG_WIPE_SOFT      0.12    /* wipe effect: width of the soft edge, as a fraction of the picture */

#endif

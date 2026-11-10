* make sure every plugin has a correct value for VST3 class definition with appropriate subCategories according to Steinberg's scheme, Instrument, Fx and anything more specific

* check algorithms for the Ladder plugin, this was reported : " Yours appears to be using the algorithm from Antti Huovilainen paper, with Mystran’s “cheap ZDF” approach that groups all non-linearities together. Pretty standard stuff, though I believe self-resonance pitch will go flat under heavy gain. The tanhoverx approximation is good.

It’s smoothing coefficients linearly per-frame (which can still be spiky especially with DAWS that call you with variable frames size) instead of slew limiting the base parameters.

It’s still better than many vibe coded plugins that don’t smooth at all.

In short: not a bad filter, but there are better/more efficient implementations already. "
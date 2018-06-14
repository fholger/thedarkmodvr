While the whole TDM game is built with MSVC2017, tdm_update still stays on MSVC2013.

The following libs were built with MSVC2013 and are used only by tdm_update:
    libcurl.lib
    PolarSSL.lib
    ZipLoader.lib

All the rest were built with MSVC2017 and are used by TDM game.

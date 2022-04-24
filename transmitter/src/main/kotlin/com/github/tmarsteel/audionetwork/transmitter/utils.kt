package com.github.tmarsteel.audionetwork.transmitter

fun doAllAndThrowCombined(vararg actions: () -> Unit) {
    var topException: Throwable? = null
    for (action in actions) {
        try {
            action()
        }
        catch (ex: Exception) {
            if (topException == null) {
                topException = ex
            } else {
                topException.addSuppressed(ex)
            }
        }
    }

    topException?.let { throw it }
}
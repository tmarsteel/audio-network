package com.github.tmarsteel.audionetwork.transmitter

import kotlinx.coroutines.delay
import kotlin.time.Duration
import kotlin.time.Duration.Companion.nanoseconds
import kotlin.time.Duration.Companion.seconds

class LeakyBucket(
    @Volatile
    var capacity: Long,
    @Volatile
    var drainRatePerSecond: Long
) {
    @Volatile
    private var lastValue: Long = 0

    @Volatile
    private var lastValueAtNanos: Long = System.nanoTime()

    val currentValue: Long
        get() {
            val nanosSinceLastValue = System.nanoTime() - lastValueAtNanos
            val drainedSinceLastValue = drainRatePerSecond * nanosSinceLastValue / NANOS_PER_SECOND
            return (lastValue - drainedSinceLastValue).coerceAtLeast(0)
        }

    /**
     * Trys to add the given [amount] to the bucket. If there is capacity, adds the [amount]
     * and returns `null`. If the [amount] would exceed the [capacity], leaves the [currentValue]
     * untouched and returns the [amount] of time to wait until the call should be retried.
     * @throws IllegalArgumentException if `amount > capacity` (call can never succeed)
     */
    fun tryPut(amount: Long): Duration? {
        val now = System.nanoTime()
        val localCurrentValue = currentValue

        if (amount > capacity) {
            throw IllegalArgumentException("The given amount $amount is more than the bucket capacity ($capacity), so it can never be added.")
        }

        val newValue = localCurrentValue + amount

        if (newValue > capacity) {
            val overshoot = newValue - capacity
            return (overshoot * NANOS_PER_SECOND / drainRatePerSecond).nanoseconds
        }

        lastValue = localCurrentValue + amount
        lastValueAtNanos = now
        return null
    }

    /**
     * Puts the given amount in the bucket, possibly waiting with [delay] if necessary.
     * @throws IllegalArgumentException if `amount > capacity` (call can never succeed)
     */
    suspend fun waitForCapacity(amount: Long) {
        do {
            val delay = tryPut(amount)
            if (delay != null) {
                delay(delay)
            }
        } while (delay != null)
    }

    private companion object {
        @JvmStatic
        val NANOS_PER_SECOND = 1.seconds.inWholeNanoseconds
    }
}
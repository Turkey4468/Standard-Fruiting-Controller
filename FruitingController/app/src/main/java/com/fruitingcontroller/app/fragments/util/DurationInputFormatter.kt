package com.fruitingcontroller.app.fragments.util

import com.fruitingcontroller.app.models.TimeInterval

/**
 * Utility for parsing duration strings in "Xd HH:MM:SS" format.
 * Formatting is handled by TimeInterval.toDisplayString().
 */
object DurationInputFormatter {

    /**
     * Parses a duration string of the form "Xd HH:MM:SS" (e.g. "2d 01:30:00").
     * Returns null if the input cannot be parsed.
     */
    fun parse(text: String): TimeInterval? {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return TimeInterval()

        return try {
            // Expected format: "Xd HH:MM:SS"
            val parts = trimmed.split(" ")
            if (parts.size != 2) return null

            val daysPart = parts[0]
            val timePart = parts[1]

            if (!daysPart.endsWith("d")) return null
            val days = daysPart.dropLast(1).toInt()

            val timeSplit = timePart.split(":")
            if (timeSplit.size != 3) return null

            val hours = timeSplit[0].toInt()
            val minutes = timeSplit[1].toInt()
            val seconds = timeSplit[2].toInt()

            if (hours < 0 || hours > 23) return null
            if (minutes < 0 || minutes > 59) return null
            if (seconds < 0 || seconds > 59) return null

            TimeInterval(days, hours, minutes, seconds)
        } catch (e: NumberFormatException) {
            null
        }
    }
}

package com.local.rtpplayer

import android.content.Context
import android.net.ConnectivityManager
import android.util.Log
import java.net.Inet4Address

object NetworkDebug {
    private const val TAG = "RtpPlayer_Net"

    fun getPreferredIpv4Address(context: Context): String? {
        val connectivityManager =
            context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
                ?: return null

        val activeNetwork = connectivityManager.activeNetwork ?: return null
        val linkProperties = connectivityManager.getLinkProperties(activeNetwork) ?: return null

        val ipv4Addresses = linkProperties.linkAddresses
            .mapNotNull { it.address as? Inet4Address }
            .filter { !it.isLoopbackAddress && !it.isLinkLocalAddress }

        if (ipv4Addresses.isEmpty()) {
            Log.w(TAG, "No usable IPv4 addresses on active network")
            return null
        }

        val joined = ipv4Addresses.joinToString(",") { it.hostAddress ?: "(null)" }
        Log.i(TAG, "Active network IPv4 addresses: $joined")
        return ipv4Addresses.first().hostAddress
    }
}

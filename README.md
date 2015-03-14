# task-unchain

Binary patch for taskgated that disables entitlement restrictions (including AppStore-only restrictions).

## Introduction

On Mac OS X, application signing [entitlements](https://developer.apple.com/library/ios/documentation/Miscellaneous/Reference/EntitlementKeyReference/Chapters/AboutEntitlements.html)
enforce client-side constraints on non-AppStore (or non-Apple-distributed) applications. This is used, for instance, to prevent a non-AppStore application from using the
MapKit APIs.

Recently, I wanted to backport Xcode 6.3 from Yosemite, getting it running on Mavericks. Unfortunately, simply stripping
signatures wasn't an option -- Xcode itself transitively depends on code signing via its use of XPC.

It's *also* not possible to simply resign a modified Xcode binary with a local adhoc
certificate (or a standard paid Mac Developer certificate); Xcode relies on Apple-privileged
entitlements -- including the MapKit entitlement -- that aren't available without a trusted
entitlement-granting provisioning profile.

These AppStore-only functionality constraints are enforced by the `/usr/libexec/taskgated` daemon; to
work around it, task-unchain patches the taskpolicyd code, disabling all checks
for restricted entitlements.

## Supported Systems and Warnings

**THIS MODIFIES A CRITICAL SECURITY DAEMON. THERE IS NO WARRANTY. MAKE BACKUPS. USE AT YOUR OWN RISK.**

This is a hack, I've only reverse engineered just enough of the enforcement mechanisms to implement
what I need, and like any hack, it may have unexpected consequences.

The patch has been tested on Mac OS X 10.9.5 (13F1066); since the patch performs a search for the machine
code containing the policy check, it <em>may</em> work on other releases. It also could just as easily leave you
with a non-booting system and a massive hangover.

**MAKE BACKUPS**.

## Applying the Patch

Once the patch is applied, you'll need to re-sign taskgated and possibly also taskgated-helper:

    sudo codesign -f -s - --preserve-metadata /usr/libexec/taskgated
    sudo codesign -f -s - --preserve-metadata /usr/libexec/taskgated-helper

Once you restart taskgated, entitlement policy will no longer be enforced.

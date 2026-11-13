#include "motion_policy.h"
#include <QCoreApplication>
#include <QMetaObject>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

MotionPolicy::MotionPolicy(QObject *parent) : QObject(parent) {
    refreshFromSystem();
}

bool MotionPolicy::systemPrefersReducedMotion() {
#ifdef Q_OS_WIN
    BOOL effectsEnabled = TRUE;
    BOOL clientAnimationEnabled = TRUE;
    const BOOL effectsKnown =
        SystemParametersInfoW(SPI_GETUIEFFECTS, 0, &effectsEnabled, 0);
    const BOOL clientKnown =
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0,
                              &clientAnimationEnabled, 0);
    // A failed OS query must never enable motion by default.
    return !effectsKnown || !clientKnown ||
           !effectsEnabled || !clientAnimationEnabled;
#else
    // Qt 6.8 has no portable reduced-motion hint. Default to still UI.
    return true;
#endif
}

void MotionPolicy::apply(bool reduced) {
    if (m_reduced == reduced) return;
    m_reduced = reduced;
    emit motionChanged();
}

void MotionPolicy::refreshFromSystem() {
    if (m_override.has_value()) return;
    apply(systemPrefersReducedMotion());
}

void MotionPolicy::setTestPreference(bool reduced) {
    m_override = reduced;
    apply(reduced);
}

bool MotionPolicy::nativeEventFilter(const QByteArray &eventType,
                                     void *message, qintptr *result) {
    Q_UNUSED(result)
#ifdef Q_OS_WIN
    // Windows broadcasts WM_SETTINGCHANGE when accessibility/effects
    // preferences change. Refresh queued, outside the native event handler.
    if ((eventType == "windows_generic_MSG" ||
         eventType == "windows_dispatcher_MSG") && message) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_SETTINGCHANGE && !m_override.has_value())
            QMetaObject::invokeMethod(this, [this]{ refreshFromSystem(); },
                                      Qt::QueuedConnection);
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
#endif
    return false;
}

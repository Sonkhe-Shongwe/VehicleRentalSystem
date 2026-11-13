#ifndef RENTAL_STUDIO_P4_MOTION_POLICY_H
#define RENTAL_STUDIO_P4_MOTION_POLICY_H

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <optional>

// Qt 6.8/6.10 do not provide QAccessibilityHints::motionPreference
// (introduced in Qt 6.12). Detect Windows animation effects using the
// documented OS SystemParametersInfoW flags. Other OSes fail safe to no
// cosmetic animation; business selection and focus always update instantly.
// Test-mode injection is permitted only by --p4-motion-smoke in the
// fictional-fixture harness.
class MotionPolicy final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY motionChanged)
    Q_PROPERTY(int selectionFadeMs READ selectionFadeMs NOTIFY motionChanged)
public:
    explicit MotionPolicy(QObject *parent = nullptr);
    bool reducedMotion() const { return m_reduced; }
    int selectionFadeMs() const { return m_reduced ? 0 : 90; }
    void refreshFromSystem();
    void setTestPreference(bool reduced);
    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;
signals:
    void motionChanged();
private:
    static bool systemPrefersReducedMotion();
    void apply(bool reduced);
    std::optional<bool> m_override;
    bool m_reduced = true;
};

#endif

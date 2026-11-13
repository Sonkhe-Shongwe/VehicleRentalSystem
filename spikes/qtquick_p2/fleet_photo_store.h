#pragma once
#include <QObject>
#include <QImage>
#include <QUrl>
#include <QPair>
#include <functional>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantMap>

class FleetPhotoStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasStaged READ hasStaged NOTIFY changed)
    Q_PROPERTY(QString feedback READ feedback NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    explicit FleetPhotoStore(const QString &fictionalRoot, QObject *parent=nullptr);
    // Resolve an actual live fictional fleet record. A reused ID alone
    // must never grant access to a previous model's reference photograph.
    using IdentityLookup = std::function<QPair<QString,QString>(const QString &)>;
    void setVehicleIdentityLookup(IdentityLookup lookup);
    bool hasStaged() const { return !m_staged.isNull(); }
    // The legacy 5-vehicle native checks keep their original no-bike-photo
    // baseline. Only the 100-vehicle showcase opts into CC0 representatives.
    void setPortfolioShowcase(bool enabled) { m_portfolioShowcase=enabled; }
    QString feedback() const { return m_feedback; }
    int revision() const { return m_revision; }
    Q_INVOKABLE bool chooseFile(const QUrl &file);
    Q_INVOKABLE void discardStaged();
    Q_INVOKABLE bool hasPhoto(const QString &vehicleId) const;
    // Actual chosen resource provenance; empty for missing or unsafe media.
    Q_INVOKABLE QVariantMap attributionFor(const QString &vehicleId) const;
    // The QML replacement path must be explicit, fictional-only and report
    // its actual storage outcome. C++ Fleet Add uses saveStagedFor directly.
    void setReplacementEnabled(bool enabled) { m_replacementEnabled=enabled; }
    Q_INVOKABLE bool replaceFor(const QString &vehicleId,const QUrl &localFile);
    // Reveal only a local fictional override; default resources are read-only.
    Q_INVOKABLE bool hasCustomPhoto(const QString &vehicleId) const;
    Q_INVOKABLE bool restoreDefaultFor(const QString &vehicleId);
    bool saveStagedFor(const QString &vehicleId);
    void forgetVehicle(const QString &vehicleId);
    QImage imageFor(const QString &vehicleId, const QSize &requestedSize={}) const;
    QImage stagedImage(const QSize &requestedSize={}) const {
        if(m_staged.isNull())return {};
        return requestedSize.isValid()
            ? m_staged.scaled(requestedSize,Qt::KeepAspectRatio,Qt::SmoothTransformation)
            : m_staged;
    }
signals:
    void changed();
private:
    QString imagePath(const QString &vehicleId) const;
    QString seededResource(const QString &vehicleId) const;
    bool hasLiveVehicle(const QString &vehicleId) const;
    QString m_root;
    IdentityLookup m_vehicleIdentityLookup;
    QString m_feedback;
    QImage m_staged;
    int m_revision=0;
    bool m_replacementEnabled=false;
    bool m_portfolioShowcase=false;
};

class FleetPhotoProvider final : public QQuickImageProvider {
public:
    explicit FleetPhotoProvider(const FleetPhotoStore &store)
        : QQuickImageProvider(QQuickImageProvider::Image), m_store(store) {}
    QImage requestImage(const QString &id, QSize *size,
                        const QSize &requestedSize) override;
private:
    const FleetPhotoStore &m_store;
};

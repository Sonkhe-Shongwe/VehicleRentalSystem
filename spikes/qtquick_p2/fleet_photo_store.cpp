#include "fleet_photo_store.h"
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <utility>

namespace {
bool safeId(const QString &id) {
    static const QRegularExpression pattern(QStringLiteral("^[CM][0-9]{3,8}$"));
    return pattern.match(id).hasMatch();
}
QString seed(const QString &id,const QPair<QString,QString> &identity,
             bool portfolioShowcase) {
    const QString make=identity.first.trimmed();
    const QString model=identity.second.trimmed();
    // Explicit representative model association, never an ID-number alias.
    // Trims/years remain unknown, so these are still illustrative only.
    if(make.compare(QStringLiteral("Toyota"),Qt::CaseInsensitive)==0 &&
       model.compare(QStringLiteral("Corolla"),Qt::CaseInsensitive)==0)
        return QStringLiteral(":/fleet-demo-photos/toyota_corolla.jpg");
    if(make.compare(QStringLiteral("Ford"),Qt::CaseInsensitive)==0 &&
       model.compare(QStringLiteral("Ranger"),Qt::CaseInsensitive)==0)
        return QStringLiteral(":/fleet-demo-photos/ford_ranger.jpg");
    if(make.compare(QStringLiteral("Hyundai"),Qt::CaseInsensitive)==0 &&
       model.compare(QStringLiteral("i20"),Qt::CaseInsensitive)==0)
        return QStringLiteral(":/fleet-demo-photos/hyundai_i20.jpg");
    if(!portfolioShowcase)return {};
    // Model-matched local Commons resources, never generic brand substitutes.
    // Two low-quality compositions and six unsourced models use silhouettes.
    static const QHash<QString,QString> catalogueImages=[] {
        QHash<QString,QString> map;
        QFile file(QStringLiteral(":/catalogue-photos/model_photo_map.tsv"));
        if(!file.open(QIODevice::ReadOnly))return map;
        static const QRegularExpression safePath(
            QStringLiteral("^batch0[1-5]/images/[a-z0-9_]+$"));
        for(const auto &line:QString::fromUtf8(file.readAll()).split('\n')) {
            const auto fields=line.trimmed().split('|');
            if(fields.size()!=3 || !safePath.match(fields.at(2)).hasMatch())continue;
            map.insert(fields.at(0).trimmed().toCaseFolded()+
                QLatin1Char('|')+fields.at(1).trimmed().toCaseFolded(),fields.at(2));
        }
        return map;
    }();
    const QString key=make.toCaseFolded()+QLatin1Char('|')+model.toCaseFolded();
    const QString candidate=catalogueImages.value(key);
    if(!candidate.isEmpty()) {
        const QString resource=QStringLiteral(":/catalogue-photos/")+
                               candidate+QStringLiteral(".jpg");
        if(QFile::exists(resource))return resource;
    }
    // Preserve earlier CC0 model-specific references, never wrong-brand maps.
    // Portfolio photos are embedded, rights-audited representatives.
    // When not downloaded, the existing offline silhouette is used.
    static const QHash<QString,QString> portfolioImages=[] {
        QHash<QString,QString> mapping;
        QFile file(QStringLiteral(":/portfolio-demo/photo_map.tsv"));
        if(!file.open(QIODevice::ReadOnly))return mapping;
        for(const auto &line:QString::fromUtf8(file.readAll()).split('\n')) {
            const auto fields=line.trimmed().split('|');
            if(fields.size()!=3)continue;
            static const QRegularExpression safeSlug(QStringLiteral("^[a-z0-9_]+$"));
            if(!safeSlug.match(fields.at(2)).hasMatch())continue;
            mapping.insert(fields.at(0).trimmed().toCaseFolded()+
                QLatin1Char('|')+fields.at(1).trimmed().toCaseFolded(),
                fields.at(2));
        }
        return mapping;
    }();
    const QString slug=portfolioImages.value(key);
    const QString resource=QStringLiteral(":/portfolio-photos/")+slug+
        QStringLiteral(".jpg");
    return !slug.isEmpty() && QFile::exists(resource)?resource:QString{};
}
QVariantMap photoAttribution(const QString &resource) {
    // Keys come from actual offline resources, not guessed model names.
    static const QHash<QString,QVariantMap> credits=[] {
        QHash<QString,QVariantMap> entries;
        const auto readManifest=[&](const QString &manifest,
                                    const QString &localPrefix,
                                    const QString &resourcePrefix) {
            QFile file(manifest);
            if(!file.open(QIODevice::ReadOnly))return;
            const QJsonDocument doc=QJsonDocument::fromJson(file.readAll());
            if(!doc.isObject())return;
            for(const QJsonValue &value:doc.object().value("images").toArray()) {
                const QJsonObject row=value.toObject();
                const QString path=row.value("local_file").toString();
                const QString creator=row.value("creator").toString().trimmed();
                const QString licence=row.value("licence").toString().trimmed();
                const QString source=row.value("source_page").toString();
                const QString licencePage=row.value("licence_url").toString();
                if(!path.startsWith(localPrefix) || !path.endsWith(".jpg") ||
                   creator.isEmpty() || licence.isEmpty() ||
                   !source.startsWith("https://commons.wikimedia.org/wiki/File:") ||
                   !(licencePage.startsWith("https://creativecommons.org/licenses/") ||
                     licencePage.startsWith("http://creativecommons.org/publicdomain/")))continue;
                entries.insert(resourcePrefix+path.mid(localPrefix.size()),
                               {{"creator",creator},{"licence",licence},
                                {"sourcePage",source},{"licencePage",licencePage},
                                {"kind",QStringLiteral("reference")}});
            }
        };
        for(int batch=1;batch<=5;++batch) {
            const QString folder=QStringLiteral("batch%1").arg(batch,2,10,QChar('0'));
            readManifest(QStringLiteral(":/fleet-credit-data/")+folder+
                             QStringLiteral("/source_manifest.json"),
                         QStringLiteral("catalogue_acquisition/")+folder+QLatin1Char('/'),
                         QStringLiteral(":/catalogue-photos/")+folder+QLatin1Char('/'));
        }
        readManifest(QStringLiteral(":/portfolio-credit-data/source_manifest.json"),
                     QStringLiteral("assets/portfolio/"),
                     QStringLiteral(":/portfolio-photos/"));
        // Three existing original CC0 photos, independently logged in assets/README.md.
        entries.insert(QStringLiteral(":/fleet-demo-photos/toyota_corolla.jpg"),
                       {{"creator",QStringLiteral("01x07x2022000")},{"licence",QStringLiteral("CC0 1.0")},
                        {"sourcePage",QStringLiteral("https://commons.wikimedia.org/wiki/File:Toyota_Corolla_IMG_6391.JPG")},
                        {"licencePage",QStringLiteral("https://creativecommons.org/publicdomain/zero/1.0/")},
                        {"kind",QStringLiteral("reference")}});
        entries.insert(QStringLiteral(":/fleet-demo-photos/ford_ranger.jpg"),
                       {{"creator",QStringLiteral("Hyins")},{"licence",QStringLiteral("CC0 1.0")},
                        {"sourcePage",QStringLiteral("https://commons.wikimedia.org/wiki/File:2nd_Gen_2025_Ford_Ranger_T6_RA_XLT_4WD_P789_China_Front_Three_Quarter_View.jpg")},
                        {"licencePage",QStringLiteral("https://creativecommons.org/publicdomain/zero/1.0/")},
                        {"kind",QStringLiteral("reference")}});
        entries.insert(QStringLiteral(":/fleet-demo-photos/hyundai_i20.jpg"),
                       {{"creator",QStringLiteral("Trop86")},{"licence",QStringLiteral("CC0 1.0")},
                        {"sourcePage",QStringLiteral("https://commons.wikimedia.org/wiki/File:Hyundai_i20_133151.jpg")},
                        {"licencePage",QStringLiteral("https://creativecommons.org/publicdomain/zero/1.0/")},
                        {"kind",QStringLiteral("reference")}});
        return entries;
    }();
    return credits.value(resource);
}
// Only app-reencoded fictional sidecars are expected on disk. Validate
// bounds from image headers before ever asking Qt to decompress the pixels.
bool safeSidecarFolder(const QString &root) {
    const QFileInfo folder(QDir(root).filePath(QStringLiteral("fleet_images")));
    return folder.isDir() && !folder.isSymLink();
}
bool safeSidecar(const QString &root,const QString &file) {
    const QFileInfo info(file);
    if(!safeSidecarFolder(root) || !info.isFile() || info.isSymLink() ||
       info.size()<=0 || info.size()>8*1024*1024)return false;
    QImageReader reader(file);
    const QSize dimensions=reader.size();
    return dimensions.isValid() && dimensions.width()<=1280 &&
           dimensions.height()<=960 && reader.canRead();
}
}

FleetPhotoStore::FleetPhotoStore(const QString &fictionalRoot,QObject *parent)
    : QObject(parent),m_root(fictionalRoot) {}

void FleetPhotoStore::setVehicleIdentityLookup(IdentityLookup lookup) {
    m_vehicleIdentityLookup=std::move(lookup);
    ++m_revision;
    emit changed();
}
bool FleetPhotoStore::hasLiveVehicle(const QString &id) const {
    if(!safeId(id) || !m_vehicleIdentityLookup)return false;
    const auto identity=m_vehicleIdentityLookup(id);
    return !identity.first.trimmed().isEmpty() &&
           !identity.second.trimmed().isEmpty();
}
QString FleetPhotoStore::seededResource(const QString &id) const {
    if(!hasLiveVehicle(id))return {};
    return seed(id,m_vehicleIdentityLookup(id),m_portfolioShowcase);
}
QString FleetPhotoStore::imagePath(const QString &id) const {
    return QDir(m_root).filePath(QStringLiteral("fleet_images/%1.png").arg(id));
}
QVariantMap FleetPhotoStore::attributionFor(const QString &id) const {
    if(!hasLiveVehicle(id))return {};
    const QString sidecar=imagePath(id);
    if(QFileInfo::exists(sidecar)) {
        if(!safeSidecar(m_root,sidecar))return {};
        return {{"creator",QStringLiteral("Operator-selected fictional image")},
                {"licence",QStringLiteral("Not verified")},
                {"sourcePage",QString{}},{"licencePage",QString{}},
                {"kind",QStringLiteral("custom")}};
    }
    const QString resource=seededResource(id);
    if(resource.isEmpty() || !QImageReader(resource).canRead())return {};
    return photoAttribution(resource);
}
bool FleetPhotoStore::hasPhoto(const QString &id) const {
    if(!hasLiveVehicle(id))return false;
    const QString file=imagePath(id);
    if(QFileInfo::exists(file)) {
        // Refuse tampered sidecars before Qt allocates decoded pixels.
        return safeSidecar(m_root,file);
    }
    const auto resource=seededResource(id);
    return !resource.isEmpty() && QImageReader(resource).canRead();
}
QImage FleetPhotoStore::imageFor(const QString &id,const QSize &requested) const {
    if(!hasLiveVehicle(id))return {};
    const QString file=imagePath(id);
    const bool local=QFileInfo::exists(file);
    if(local && !safeSidecar(m_root,file))return {};
    const QString origin=local?file:seededResource(id);
    if(origin.isEmpty())return {};
    QImageReader reader(origin);
    reader.setAutoTransform(true);
    // The provider sees only fixed repo resources or our re-encoded PNG.
    QImage image=reader.read();
    if(image.isNull())return {};
    if(requested.isValid() && requested.width()>0 && requested.height()>0)
        image=image.scaled(requested,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    return image;
}
bool FleetPhotoStore::chooseFile(const QUrl &file) {
    m_staged=QImage{};
    m_feedback.clear();
    if(!file.isLocalFile()) {
        m_feedback=QStringLiteral("Choose a local PNG, JPEG or WebP image.");
        ++m_revision;emit changed();return false;
    }
    const QFileInfo info(file.toLocalFile());
    if(!info.isFile() || !info.isReadable() || info.isSymLink() ||
       info.size()<=0 || info.size()>12*1024*1024) {
        m_feedback=QStringLiteral("Choose a readable photo under 12 MB.");
        ++m_revision;emit changed();return false;
    }
    QImageReader reader(info.absoluteFilePath());
    reader.setAutoTransform(true);
    const auto fmt=reader.format().toLower();
    const QSize dimensions=reader.size();
    const qint64 pixels=qint64(dimensions.width())*dimensions.height();
    if((fmt!="png" && fmt!="jpeg" && fmt!="jpg" && fmt!="webp") ||
       !dimensions.isValid() || pixels<=0 || pixels>16000000) {
        m_feedback=QStringLiteral("Use a valid PNG, JPEG or WebP with at most 16 megapixels.");
        ++m_revision;emit changed();return false;
    }
    QImage raw=reader.read();
    if(raw.isNull()) {
        m_feedback=QStringLiteral("The selected photo could not be decoded.");
        ++m_revision;emit changed();return false;
    }
    m_staged=(raw.width()>1280 || raw.height()>960)
        ? raw.scaled(1280,960,Qt::KeepAspectRatio,Qt::SmoothTransformation)
        : raw;
    m_feedback=QStringLiteral("Photo ready for this fictional demo vehicle.");
    ++m_revision;emit changed();return true;
}
bool FleetPhotoStore::replaceFor(const QString &id,const QUrl &file) {
    // The fixed fictional-only session is read-only for image changes too.
    if(!m_replacementEnabled || !hasLiveVehicle(id)) {
        m_staged=QImage{};
        m_feedback=!m_replacementEnabled
            ? QStringLiteral("Photo changes are disabled in fixed fictional-only mode.")
            : QStringLiteral("Photo unchanged: invalid vehicle ID.");
        ++m_revision;emit changed();return false;
    }
    if(!chooseFile(file))return false;
    const bool saved=saveStagedFor(id);
    if(!saved) {
        // Keep the reason visible but never leak replacement staging into
        // the separate Add Vehicle preview after a rejected write.
        m_staged=QImage{};
        ++m_revision;emit changed();
    }
    return saved;
}
bool FleetPhotoStore::hasCustomPhoto(const QString &id) const {
    if(!hasLiveVehicle(id) || !safeSidecarFolder(m_root))return false;
    const QFileInfo info(imagePath(id));
    // An invalid on-disk image is still an override that the operator
    // needs to be able to discard without deleting the fleet vehicle.
    return info.isFile() && !info.isSymLink();
}
bool FleetPhotoStore::restoreDefaultFor(const QString &id) {
    if(!m_replacementEnabled || !hasLiveVehicle(id)) {
        m_feedback=!m_replacementEnabled
            ? QStringLiteral("Restoring images is disabled in fixed fictional-only mode.")
            : QStringLiteral("Default image unchanged: invalid vehicle ID.");
        ++m_revision;emit changed();return false;
    }
    if(!safeSidecarFolder(m_root)) {
        m_feedback=QStringLiteral("Default image unchanged: fictional photo folder is unsafe.");
        ++m_revision;emit changed();return false;
    }
    const QString file=imagePath(id);
    const QFileInfo info(file);
    if(!info.isFile() || info.isSymLink()) {
        m_feedback=QStringLiteral("Default image unchanged: no safe custom photo to remove.");
        ++m_revision;emit changed();return false;
    }
    if(!QFile::remove(file)) {
        m_feedback=QStringLiteral("Default image unchanged: custom photo could not be removed.");
        ++m_revision;emit changed();return false;
    }
    m_feedback=QStringLiteral("Default fictional vehicle image restored.");
    ++m_revision;emit changed();
    return true;
}
void FleetPhotoStore::discardStaged() {
    m_staged=QImage{};
    m_feedback.clear();
    ++m_revision;emit changed();
}
bool FleetPhotoStore::saveStagedFor(const QString &id) {
    if(m_staged.isNull())return true; // A fallback silhouette is valid.
    if(!hasLiveVehicle(id)) {
        m_feedback=QStringLiteral("Photo not attached: unsafe vehicle ID.");
        ++m_revision;emit changed();return false;
    }
    QByteArray png;
    QBuffer buffer(&png);
    if(!buffer.open(QIODevice::WriteOnly) || !m_staged.save(&buffer,"PNG") ||
       png.isEmpty() || png.size()>8*1024*1024) {
        m_feedback=QStringLiteral("Photo could not be prepared.");
        ++m_revision;emit changed();return false;
    }
    QDir dir(m_root);
    if(!dir.mkpath(QStringLiteral("fleet_images")) ||
       !safeSidecarFolder(m_root) || QFileInfo(imagePath(id)).isSymLink()) {
        m_feedback=QStringLiteral("Photo folder is not a safe fictional directory.");
        ++m_revision;emit changed();return false;
    }
    QSaveFile out(imagePath(id));
    if(!out.open(QIODevice::WriteOnly) || out.write(png)!=png.size() ||
       !out.commit()) {
        out.cancelWriting();
        m_feedback=QStringLiteral("Photo could not be stored; existing vehicle details remain unchanged.");
        ++m_revision;emit changed();return false;
    }
    m_staged=QImage{};
    m_feedback=QStringLiteral("Photo attached to fictional vehicle.");
    ++m_revision;emit changed();return true;
}
void FleetPhotoStore::forgetVehicle(const QString &id) {
    if(!safeId(id))return;
    if(!safeSidecarFolder(m_root))return;
    const QString path=imagePath(id);
    if(QFileInfo::exists(path) && !QFileInfo(path).isSymLink())
        QFile::remove(path);
    ++m_revision;emit changed();
}
QImage FleetPhotoProvider::requestImage(const QString &id,QSize *size,
                                         const QSize &requestedSize) {
    // Image URL query is used only to force refresh when a photo is changed.
    const QString key=id.section(QLatin1Char('?'),0,0);
    QImage result=key==QStringLiteral("__staged")
        ? m_store.stagedImage(requestedSize)
        : m_store.imageFor(key,requestedSize);
    if(size)*size=result.size();
    return result;
}

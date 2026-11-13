#pragma once
// P7-I isolated Qt Quick application-facing preview controller.
// ONLY built into the opt-in P7 test executable; never registered in RentalStudio.
// No arbitrary path input, no customers, no legacy source files.
#include <QObject>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>
#include <QDate>
#include <QString>
#include <memory>
#include <optional>
#include "src/application/p7_sandbox/sqlite_repository.h"

namespace p7fiction {
class PreviewController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready CONSTANT)
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString quoteLabel READ quoteLabel NOTIFY changed)
    Q_PROPERTY(QString rentalId READ rentalId NOTIFY changed)
    Q_PROPERTY(QString vehicleState READ vehicleState NOTIFY changed)
    Q_PROPERTY(bool canPreview READ canPreview NOTIFY changed)
    Q_PROPERTY(bool canConfirm READ canConfirm NOTIFY changed)
    Q_PROPERTY(bool canReturn READ canReturn NOTIFY changed)
    Q_PROPERTY(bool canArchive READ canArchive NOTIFY changed)
    Q_PROPERTY(bool canRetire READ canRetire NOTIFY changed)
public:
    explicit PreviewController(QObject *parent=nullptr)
        : QObject(parent), m_temp(QDir::tempPath()+"/rental-p7i-XXXXXX") {
        if (!m_temp.isValid()) { m_message="Temporary fictional storage unavailable"; return; }
        QFile marker(m_temp.filePath(".fictional-only"));
        const QByteArray exact="P7-H GENERATED FICTIONAL ONLY\n";
        if (!marker.open(QIODevice::WriteOnly|QIODevice::NewOnly) ||
            marker.write(exact)!=exact.size()) {
            m_message="Fictional marker creation failed"; return;
        }
        marker.close();
        m_repo=std::make_unique<Repository>(m_temp.path());
        QString car;
        if (!m_repo->createNew() ||
            m_repo->vehicle("Car","Toyota","Corolla","450.00",4,&car)!=Status::Ok ||
            car!="C001") {
            m_repo.reset();
            m_message="Fictional repository initialization refused"; return;
        }
        m_ready=true;
        m_message="Generated fictional Corolla C001. No customer information collected.";
    }
    ~PreviewController() override = default;
    bool ready() const { return m_ready; }
    // No QML exposure: only the fixed, generated-fictional test harness
    // can attach existing production-facing C++ read model to this repo.
    const Repository *testOnlyReadRepository() const {return m_repo.get();}
    QString stage() const { return m_stage; }
    QString message() const { return m_message; }
    QString rentalId() const { return m_rental; }
    QString vehicleState() const { return m_repo ? m_repo->state("C001") : QString(); }
    QString quoteLabel() const {
        if(!m_quote) return "No current quote";
        const qint64 cents=m_quote->totalCents;
        return QString("R %1.%2 for %3 days")
            .arg(cents/100).arg(cents%100,2,10,QChar('0'))
            .arg(m_quote->days);
    }
    bool canPreview() const { return m_ready && m_stage=="READY"; }
    bool canConfirm() const { return m_ready && m_stage=="QUOTED" && m_quote.has_value(); }
    bool canReturn() const { return m_ready && m_stage=="ACTIVE"; }
    bool canArchive() const { return m_ready && m_stage=="RETURNED"; }
    bool canRetire() const { return m_ready && m_stage=="ARCHIVED"; }
    Q_INVOKABLE bool previewQuote() {
        if(!canPreview())return false;
        m_quote=m_repo->quote("C001",3,m_day);
        if(!m_quote){m_message="No available fictional vehicle";emit changed();return false;}
        m_stage="QUOTED";
        m_message="Quote prepared from current SQLite cents. Not a payment.";
        emit changed();return true;
    }
    Q_INVOKABLE bool confirmBooking() {
        if(!canConfirm())return false;
        const auto result=m_repo->book(*m_quote,m_day);
        if(result.status!=Status::Ok || result.id.isEmpty()) {
            m_message="Booking rejected. No receipt or rental ID issued.";
            emit changed();return false;
        }
        m_rental=result.id;
        m_stage="ACTIVE";
        m_message="Fictional "+m_rental+" committed. No payment taken.";
        emit changed();emit committed();return true;
    }
    Q_INVOKABLE bool returnBooking() {
        if(!canReturn())return false;
        if(m_repo->returnRental(m_rental,m_day,m_day)!=Status::Ok)return false;
        m_stage="RETURNED";m_message="Fictional rental returned; history preserved.";
        emit changed();emit committed();return true;
    }
    Q_INVOKABLE bool archiveBooking() {
        if(!canArchive())return false;
        if(m_repo->archive(m_rental)!=Status::Ok)return false;
        m_stage="ARCHIVED";m_message="Fictional history archived, not deleted.";
        emit changed();emit committed();return true;
    }
    Q_INVOKABLE bool retireAsset() {
        if(!canRetire())return false;
        if(m_repo->retire("C001")!=Status::Ok)return false;
        m_stage="RETIRED";m_message="Fictional asset retired. Archived rental remains.";
        emit changed();emit committed();return true;
    }
    // Native harness-only checks. No general user path, PII or migration.
    bool rejectNextBookingForTest() {
        if(!canConfirm())return false;
        m_repo->failNextCommitForTest();return true;
    }
    bool committedStateConsistent() const {
        if(!m_ready||!m_repo)return false;
        auto rental=m_repo->rental("R001");
        if(m_stage=="READY"||m_stage=="QUOTED")
            return !rental && m_repo->count("rentals")==0 && m_repo->next("R")==1;
        if(!rental||rental->totalCents!=135000||m_repo->next("R")!=2)
            return false;
        if(m_stage=="ACTIVE")return rental->status=="ACTIVE";
        if(m_stage=="RETURNED")return rental->status=="RETURNED";
        if(m_stage=="ARCHIVED")return rental->status=="ARCHIVED";
        if(m_stage=="RETIRED")return rental->status=="ARCHIVED"&&m_repo->state("C001")=="RETIRED";
        return false;
    }
    bool independentReopen() {
        if(!m_ready||!m_repo)return false;
        m_repo.reset();
        m_repo=std::make_unique<Repository>(m_temp.path());
        return m_repo->ready() && m_repo->integrity() && committedStateConsistent();
    }
signals:
    void changed();
    void committed(); // Emit only after successful SQLite COMMIT.
private:
    QTemporaryDir m_temp; // Repository destroyed before QTemporaryDir cleans it.
    std::unique_ptr<Repository> m_repo;
    bool m_ready=false;
    QString m_stage="READY";
    QString m_message="Initializing fictional preview";
    QString m_rental;
    std::optional<Quote> m_quote;
    const QDate m_day=QDate(2026,11,2);
};
} // namespace p7fiction

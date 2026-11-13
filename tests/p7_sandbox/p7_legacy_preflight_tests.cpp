// P7-C test-only, read-only raw legacy text preflight.
// No runtime binding, imported data, real contacts, SQL insertion or arbitrary CLI path.
// Only tests synthesize records under a private QTemporaryDir.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <limits>
#include <optional>

namespace {
struct Finding {
    QString fileKind;
    int line = 0;
    QString code;
};
struct Report {
    QList<Finding> findings;
    int vehicles = 0;
    int rentals = 0;
    int active = 0;
    int historical = 0;
    int vehicleLines = 0;
    int rentalLines = 0;
    int comparableContracts = 0;
    int matchingContracts = 0;
    qint64 declaredBookedCents = 0;
    qint64 computedBookedCents = 0;
    bool totalsComplete = true;
    bool addAmount(qint64 declared, qint64 computed, int line) {
        const auto max=std::numeric_limits<qint64>::max();
        if(declared>max-declaredBookedCents ||
           computed>max-computedBookedCents) {
            totalsComplete=false;
            add("rentals",line,"RECONCILIATION_SUM_OVERFLOW");
            return false;
        }
        declaredBookedCents+=declared;
        computedBookedCents+=computed;
        ++comparableContracts;
        if(declared==computed)++matchingContracts;
        return true;
    }
    QByteArray manifest() const {
        // Only aggregates, numeric line references and fixed issue codes.
        // Amounts are decimal STRINGS, preventing double/JSON precision loss.
        QList<Finding> ordered=findings;
        std::sort(ordered.begin(),ordered.end(),[](const Finding &a,const Finding &b){
            if(a.fileKind!=b.fileKind)return a.fileKind<b.fileKind;
            if(a.line!=b.line)return a.line<b.line;
            return a.code<b.code;
        });
        QJsonArray issues;
        for(const auto &f:ordered)
            issues.append(QJsonObject{{"file_kind",f.fileKind},
                                      {"line",f.line},{"code",f.code}});
        const bool complete=totalsComplete && comparableContracts==rentals;
        QJsonObject reconciliation{{"complete",complete},
                                   {"comparable_contracts",comparableContracts},
                                   {"matching_contracts",matchingContracts}};
        if(complete){
            reconciliation.insert("declared_booked_cents",QString::number(declaredBookedCents));
            reconciliation.insert("computed_booked_cents",QString::number(computedBookedCents));
            reconciliation.insert("exact_match",declaredBookedCents==computedBookedCents &&
                                  comparableContracts==matchingContracts);
        }else{
            reconciliation.insert("declared_booked_cents",QJsonValue::Null);
            reconciliation.insert("computed_booked_cents",QJsonValue::Null);
            reconciliation.insert("exact_match",QJsonValue::Null);
        }
        const QJsonObject counts{{"vehicle_lines",vehicleLines},
                                 {"rental_lines",rentalLines},
                                 {"parsed_vehicles",vehicles},
                                 {"parsed_rentals",rentals},
                                 {"active_rentals",active},
                                 {"historical_rentals",historical}};
        const QJsonObject doc{{"schema","p7.synthetic.preflight.v1"},
                              {"status",eligible()?"NO_FINDINGS_SYNTHETIC_ONLY":"BLOCKED"},
                              {"counts",counts},
                              {"reconciliation",reconciliation},
                              {"issues",issues}};
        return QJsonDocument(doc).toJson(QJsonDocument::Compact);
    }
    void add(const QString &kind, int line, const char *code) {
        findings.append({kind,line,QString::fromLatin1(code)});
    }
    bool eligible() const { return findings.isEmpty(); }
    bool has(const QString &code) const {
        for(const auto &finding:findings)
            if(finding.code==code)return true;
        return false;
    }
    void summary() const {
        qInfo()<<"P7-C fake-only preflight:"<<vehicles<<"vehicles"
               <<rentals<<"rentals"<<active<<"active"<<historical<<"historical"
               <<findings.size()<<"issues";
        for(const auto &f:findings)
            qInfo()<<"P7-C"<<f.fileKind<<"line"<<f.line<<f.code;
        // Not a single original field, line, customer, phone or licence is emitted.
    }
};
struct TextLines { QStringList lines; bool okay=false; };
TextLines rawLines(const QByteArray &bytes, const QString &kind, Report &r) {
    if(bytes.isEmpty() || bytes.size()>65536) {
        r.add(kind,0,"FILE_SIZE");
        return {};
    }
    if(bytes.contains('\0') || bytes.contains('\v') || bytes.contains('\f')) {
        r.add(kind,0,"FILE_CONTROL");
        return {};
    }
    const QString decoded=QString::fromUtf8(bytes);
    if(decoded.toUtf8()!=bytes || decoded.startsWith(QChar::ByteOrderMark)) {
        r.add(kind,0,"INVALID_UTF8");
        return {};
    }
    QString canonical=decoded;
    canonical.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
    if(canonical.contains(QLatin1Char('\r'))) {
        r.add(kind,0,"LINE_ENDING");
        return {};
    }
    QStringList lines=canonical.split(QLatin1Char('\n'),Qt::KeepEmptyParts);
    if(lines.last().isEmpty())lines.removeLast(); // one trailing LF is allowed
    if(lines.isEmpty()){
        r.add(kind,0,"FILE_EMPTY");
        return {};
    }
    if(kind=="vehicles")r.vehicleLines=lines.size();
    else r.rentalLines=lines.size();
    for(int i=0;i<lines.size();++i) {
        if(lines.at(i).isEmpty())r.add(kind,i+1,"EMPTY_ROW");
        if(lines.at(i).size()>1024)r.add(kind,i+1,"ROW_TOO_LONG");
    }
    return {lines,true};
}
std::optional<qint64> parseCents(const QString &raw) {
    static const QRegularExpression decimal(QStringLiteral("^[0-9]+\\.[0-9]{2}$"));
    if(!decimal.match(raw).hasMatch())return std::nullopt;
    const qsizetype dot=raw.indexOf(QLatin1Char('.'));
    bool ok=false;
    const qint64 whole=raw.left(dot).toLongLong(&ok);
    if(!ok || whole<0 || whole>(std::numeric_limits<qint64>::max()-99)/100)
        return std::nullopt;
    const int sub=raw.mid(dot+1).toInt(&ok);
    if(!ok)return std::nullopt;
    const qint64 result=whole*100+sub;
    if(result<=0)return std::nullopt;
    return result;
}
std::optional<qint64> exactTotal(qint64 cents, int days){
    if(cents<=0 || days<1 || days>365 ||
       cents>std::numeric_limits<qint64>::max()/days)return std::nullopt;
    return cents*days;
}
bool validDate(const QString &raw,QDate *result=nullptr) {
    static const QRegularExpression date(QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}$"));
    if(!date.match(raw).hasMatch())return false;
    const QDate d=QDate::fromString(raw,Qt::ISODate);
    if(!d.isValid()||d.toString(Qt::ISODate)!=raw)return false;
    if(result)*result=d;
    return true;
}
bool validId(const QString &id, const QString &prefix) {
    static const QRegularExpression suffix(QStringLiteral("^[0-9]{3,8}$"));
    return id.size()>=4 && id.startsWith(prefix,Qt::CaseInsensitive) &&
           suffix.match(id.mid(1)).hasMatch();
}
bool flag(const QString &s){return s=="0" || s=="1";}

Report inspect(const QByteArray &fleetRaw,const QByteArray &rentalRaw) {
    Report r;
    const auto v=rawLines(fleetRaw,"vehicles",r);
    const auto rentals=rawLines(rentalRaw,"rentals",r);
    if(!v.okay||!rentals.okay){
        r.totalsComplete=false;
        return r; // refuse to reconcile partially parsed raw bytes
    }
    QSet<QString> fleetIds, rentalIds, activeVehicles;
    struct RentalReference {int line; QString id; bool returned;};
    QList<RentalReference> references;
    for(int i=0;i<v.lines.size();++i){
        const int line=i+1;const QString &text=v.lines.at(i);
        if(text.isEmpty()||text.size()>1024)continue;
        const auto f=text.split('|',Qt::KeepEmptyParts);
        if(f.size()!=7){r.add("vehicles",line,"FIELD_COUNT");continue;}
        const QString kind=f.at(0),id=f.at(1);
        const bool car=kind=="Car",motorcycle=kind=="Motorcycle";
        if(!car&&!motorcycle)r.add("vehicles",line,"VEHICLE_KIND");
        if(!validId(id,car?"C":(motorcycle?"M":"X")))
            r.add("vehicles",line,"VEHICLE_ID_FORMAT");
        if(id.trimmed().isEmpty() || f.at(2).trimmed().isEmpty() ||
           f.at(3).trimmed().isEmpty())r.add("vehicles",line,"VEHICLE_REQUIRED");
        const QString normalized=id.toUpper();
        if(fleetIds.contains(normalized))r.add("vehicles",line,"DUPLICATE_VEHICLE_ID");
        fleetIds.insert(normalized);
        if(!parseCents(f.at(4)))r.add("vehicles",line,"VEHICLE_RATE");
        if(!flag(f.at(5)))r.add("vehicles",line,"VEHICLE_FLAG");
        bool extraOk=false;const int extra=f.at(6).toInt(&extraOk);
        if(!extraOk || extra<=0)r.add("vehicles",line,"VEHICLE_SPEC");
        ++r.vehicles;
    }
    for(int i=0;i<rentals.lines.size();++i){
        const int line=i+1;const QString &text=rentals.lines.at(i);
        if(text.isEmpty()||text.size()>1024)continue;
        const auto f=text.split('|',Qt::KeepEmptyParts);
        const bool old=f.size()==14;
        if(f.size()!=13 && !old){
            r.add("rentals",line,"FIELD_COUNT");
            r.totalsComplete=false;
            continue;
        }
        if(f.at(0)!="Rental")r.add("rentals",line,"RENTAL_KIND");
        if(old)r.add("rentals",line,"LEGACY_LICENCE_REVIEW");
        const QString id=f.at(1),vehicle=f.at(old?5:4);
        if(!validId(id,"R"))r.add("rentals",line,"RENTAL_ID_FORMAT");
        const QString rentalKey=id.toUpper();
        if(rentalIds.contains(rentalKey))r.add("rentals",line,"DUPLICATE_RENTAL_ID");
        rentalIds.insert(rentalKey);
        if(f.at(2).trimmed().isEmpty()||f.at(3).trimmed().isEmpty())
            r.add("rentals",line,"RENTAL_REQUIRED");
        const QString start=f.at(old?6:5), due=f.at(old?7:6),
                      actual=f.at(old?8:7);
        QDate startDate,dueDate,actualDate;
        const bool hasStart=validDate(start,&startDate);
        const bool hasDue=validDate(due,&dueDate);
        if(!hasStart||!hasDue)r.add("rentals",line,"CONTRACT_DATE");
        const bool hasActual=!actual.isEmpty()&&validDate(actual,&actualDate);
        if(!actual.isEmpty() && !hasActual)r.add("rentals",line,"ACTUAL_DATE");
        bool daysOk=false;
        const int days=f.at(old?9:8).toInt(&daysOk);
        if(!daysOk||days<1||days>365)r.add("rentals",line,"DURATION");
        if(hasStart&&hasDue&&daysOk&&days>=1&&days<=365&&
           dueDate!=startDate.addDays(days))
            r.add("rentals",line,"DATE_DURATION_MISMATCH");
        const auto rate=parseCents(f.at(old?10:9));
        const auto amount=parseCents(f.at(old?11:10));
        if(!rate||!amount)r.add("rentals",line,"RENTAL_MONEY");
        if(rate&&amount&&daysOk) {
            const auto computed=exactTotal(*rate,days);
            if(!computed){
                r.add("rentals",line,"MONEY_OVERFLOW_OR_DURATION");
                r.totalsComplete=false;
            } else {
                r.addAmount(*amount,*computed,line);
                if(*computed!=*amount)r.add("rentals",line,"TOTAL_MISMATCH");
            }
        }else r.totalsComplete=false;
        const QString returned=f.at(old?12:11),archived=f.at(old?13:12);
        if(!flag(returned)||!flag(archived))r.add("rentals",line,"STATUS_FLAG");
        if(returned=="0"&&archived=="1")
            r.add("rentals",line,"ARCHIVE_ACTIVE");
        if(returned=="1"){
            ++r.historical;
            if(actual.isEmpty())r.add("rentals",line,"MISSING_ACTUAL_RETURN");
            else if(hasActual&&hasStart&&actualDate<startDate)
                r.add("rentals",line,"RETURN_BEFORE_START");
        } else if(returned=="0"){
            ++r.active;
            if(!actual.isEmpty())r.add("rentals",line,"ACTIVE_HAS_RETURN_DATE");
            if(activeVehicles.contains(vehicle.toUpper()))
                r.add("rentals",line,"ACTIVE_OCCUPANCY_CONFLICT");
            activeVehicles.insert(vehicle.toUpper());
        }
        if(!validId(vehicle,vehicle.startsWith("M",Qt::CaseInsensitive)?"M":"C"))
            r.add("rentals",line,"RENTAL_VEHICLE_ID");
        references.append({line,vehicle.toUpper(),returned=="1"});
        ++r.rentals;
    }
    for(const auto &ref:references) {
        if(!fleetIds.contains(ref.id))
            r.add("rentals",ref.line,ref.returned?"ORPHAN_HISTORY":"ORPHAN_ACTIVE");
    }
    return r;
}
// The scanner has *no* production file-opening function. This fixture helper
// alone writes invented strings under an automatically deleted temp root.
bool fixture(const QByteArray &v,const QByteArray &r,Report *report) {
    QTemporaryDir t;if(!t.isValid())return false;
    const QString vp=t.filePath("vehicles.fake.txt"),rp=t.filePath("rentals.fake.txt");
    const auto write=[](const QString &path,const QByteArray &b){
        QFile f(path);return f.open(QIODevice::WriteOnly) &&
               f.write(b)==b.size() && f.flush();
    };
    if(!write(vp,v)||!write(rp,r))return false;
    const auto hash=[](const QByteArray &b){
        return QCryptographicHash::hash(b,QCryptographicHash::Sha256);
    };
    const auto read=[](const QString &path)->std::optional<QByteArray>{
        QFile f(path);if(!f.open(QIODevice::ReadOnly))return std::nullopt;
        QByteArray bytes=f.readAll();
        if(f.error()!=QFile::NoError)return std::nullopt;
        return bytes;
    };
    const auto originalV=read(vp),originalR=read(rp);
    if(!originalV||!originalR)return false;
    *report=inspect(*originalV,*originalR);
    const auto finalV=read(vp),finalR=read(rp);
    return finalV&&finalR &&
           hash(*originalV)==hash(*finalV) &&
           hash(*originalR)==hash(*finalR) &&
           *originalV==*finalV && *originalR==*finalR;
}
const QByteArray cleanV=
 "Car|C001|Fake Brand|Fake Model|420.00|0|4\n"
 "Motorcycle|M001|Fake Brand|Fake Moto|90.00|0|650\n"
 "Car|C002|Fake Brand|Fake Wagon|250.00|0|4\n";
const QByteArray cleanR=
 "Rental|R001|Fictional Person|0000000000|C001|2026-10-01|2026-10-03||2|420.00|840.00|0|0\n"
 "Rental|R002|Fictional Person|0000000000|M001|2026-09-01|2026-09-03|2026-09-03|2|90.00|180.00|1|1\n";
bool assertCase(bool cond,const char *what) {
    if(!cond)qCritical()<<"P7-C FAILED"<<what;
    return cond;
}
bool clean(){
    Report a,b;
    QByteArray v=cleanV,r=cleanR;
    v.replace("\n","\r\n");r.replace("\n","\r\n");
    if(!fixture(v,r,&a)||!fixture(cleanV,cleanR,&b))return false;
    a.summary();
    return assertCase(a.eligible()&&b.eligible()&&a.vehicles==3&&
                      a.rentals==2&&a.active==1&&a.historical==1,
                      "clean LF/CRLF fictitious rows");
}
bool duplicates(){
    Report r;
    QByteArray v=cleanV+"Car|c001|Fictional|Duplicate|1.00|0|4\n";
    QByteArray rent=cleanR+"Rental|r001|Fictional|0000000000|C001|2026-10-01|2026-10-03||2|420.00|840.00|0|0\n";
    if(!fixture(v,rent,&r))return false;
    r.summary();
    return assertCase(!r.eligible()&&r.has("DUPLICATE_VEHICLE_ID")&&
             r.has("DUPLICATE_RENTAL_ID")&&
             r.has("ACTIVE_OCCUPANCY_CONFLICT"),
             "casefold duplicate and active overlap");
}
bool legacy(){
    Report r;
    QByteArray rent=
      "Rental|R003|Fictional|0000000000|FAKE-LICENCE-DO-NOT-IMPORT|C002|2026-09-01|2026-09-03||2|250.00|500.00|1|1\n"
      "Rental|R004|Fictional|0000000000|C404|2026-10-01|2026-10-03|2026-10-03|2|100.00|200.00|1|1\n"
      "Rental|R005|Fictional|0000000000|M404|2026-10-01|2026-10-03||2|100.00|200.00|0|0\n";
    if(!fixture(cleanV,rent,&r))return false;
    r.summary();
    return assertCase(!r.eligible()&&r.has("LEGACY_LICENCE_REVIEW")&&
           r.has("MISSING_ACTUAL_RETURN")&&r.has("ORPHAN_HISTORY")&&
           r.has("ORPHAN_ACTIVE"),"licence, historical uncertainty and orphan refusal");
}
bool amounts(){
    Report r;
    QByteArray rent=
      "Rental|R001|Fictional|0000000000|C001|2026-10-01|2026-10-03||2|450.00|850.00|0|0\n"
      "Rental|R002|Fictional|0000000000|M001|2026-10-01|2026-10-04||2|90.00|180.00|0|0\n"
      "Rental|R003|Fictional|0000000000|C002|2026-10-01|2026-10-03||2|92233720368547758.08|800.00|0|0\n";
    if(!fixture(cleanV,rent,&r))return false;
    r.summary();
    return assertCase(!r.eligible()&&r.has("TOTAL_MISMATCH")&&
           r.has("DATE_DURATION_MISMATCH")&&r.has("RENTAL_MONEY"),
           "money mismatch, date mismatch, overflow");
}
bool malformed(){
    Report r;
    QByteArray v=cleanV+"Unknown|X003|Fictional|Bad|12.00|maybe|0\n";
    QByteArray rent=
       "Rental|R003|Fictional|0000000000|C002|2026-11-01|2026-11-03||2|250.00|500.00|0|1\n"
       "INVALID_LINE\n";
    if(!fixture(v,rent,&r))return false;
    r.summary();
    return assertCase(!r.eligible()&&r.has("VEHICLE_KIND")&&
          r.has("VEHICLE_FLAG")&&r.has("VEHICLE_SPEC")&&
          r.has("ARCHIVE_ACTIVE")&&r.has("FIELD_COUNT"),
          "invalid type/flags/extra/archive and malformed line");
}
bool manifestClean(){
    Report a,b;
    if(!fixture(cleanV,cleanR,&a))return false;
    QByteArray vf=cleanV,rf=cleanR;
    vf.replace("\n","\r\n");rf.replace("\n","\r\n");
    if(!fixture(vf,rf,&b))return false;
    const QByteArray bytes=a.manifest();
    const QJsonDocument json=QJsonDocument::fromJson(bytes);
    if(!assertCase(!json.isNull()&&json.isObject() &&
       bytes==b.manifest()&&bytes==a.manifest(),
       "stable canonical JSON, line endings and repeatability"))return false;
    const auto obj=json.object(),counts=obj.value("counts").toObject(),
               reconciliation=obj.value("reconciliation").toObject();
    return assertCase(obj.value("schema")=="p7.synthetic.preflight.v1"&&
       obj.value("status")=="NO_FINDINGS_SYNTHETIC_ONLY"&&
       obj.value("issues").toArray().isEmpty()&&
       counts.value("vehicle_lines").toInt()==3&&
       counts.value("rental_lines").toInt()==2&&
       counts.value("active_rentals").toInt()==1&&
       counts.value("historical_rentals").toInt()==1&&
       reconciliation.value("complete").toBool()&&
       reconciliation.value("comparable_contracts").toInt()==2&&
       reconciliation.value("matching_contracts").toInt()==2&&
       reconciliation.value("exact_match").toBool()&&
       reconciliation.value("declared_booked_cents").toString()=="102000"&&
       reconciliation.value("computed_booked_cents").toString()=="102000",
       "clean reconciliation, exact-string cents and aggregate counts");
}
bool manifestBlocked(){
    Report r;
    const QByteArray fake="Rental|R003|PRIVATE-CANARY-DO-NOT-PRINT|"
      "555-MOCK-PHONE|PRIVATE-LICENCE-CANARY|C002|2026-10-01|2026-10-03|"
      "|2|250.00|495.00|1|1\n";
    if(!fixture(cleanV,fake,&r))return false;
    const QByteArray bytes=r.manifest();
    const QJsonDocument json=QJsonDocument::fromJson(bytes);
    if(!assertCase(json.isObject() && bytes==r.manifest() &&
       !bytes.contains("PRIVATE-CANARY") &&
       !bytes.contains("PRIVATE-LICENCE") &&
       !bytes.contains("555-MOCK") && !bytes.contains("R003") &&
       !bytes.contains("C002"),"no contact or record IDs leak in output"))
       return false;
    const QJsonObject obj=json.object();
    const QJsonArray issues=obj.value("issues").toArray();
    const QJsonObject reconciliation=obj.value("reconciliation").toObject();
    if(!assertCase(obj.value("status")=="BLOCKED" &&
        issues.size()==3 &&
        reconciliation.value("complete").toBool() &&
        reconciliation.value("declared_booked_cents").toString()=="49500" &&
        reconciliation.value("computed_booked_cents").toString()=="50000" &&
        !reconciliation.value("exact_match").toBool(),
        "blocked but comparable inconsistent amounts retained"))return false;
    QStringList codes;
    for(const auto &item:issues){
        auto issue=item.toObject();
        if(!assertCase(issue.value("file_kind")=="rentals" &&
                       issue.value("line").toInt()==1,
                       "line provenance and no unknown issue fields"))
            return false;
        codes.append(issue.value("code").toString());
    }
    return assertCase(codes==QStringList({"LEGACY_LICENCE_REVIEW",
                                         "MISSING_ACTUAL_RETURN",
                                         "TOTAL_MISMATCH"}),
                      "stable alphabetical issue ordering");
}
bool manifestIncomplete(){
    Report r;
    const QByteArray damaged=
       "Rental|R001|Invented|0000000000|C001|2026-10-01|2026-10-03|"
       "|2|invalid|840.00|0|0\n";
    if(!fixture(cleanV,damaged,&r))return false;
    const auto obj=QJsonDocument::fromJson(r.manifest()).object();
    const auto sums=obj.value("reconciliation").toObject();
    return assertCase(obj.value("status")=="BLOCKED"&&
           sums.value("complete")==false &&
           sums.value("declared_booked_cents").isNull()&&
           sums.value("computed_booked_cents").isNull()&&
           sums.value("exact_match").isNull()&&
           sums.value("comparable_contracts").toInt()==0,
           "incomplete totals must be explicitly null, never partial");
}
bool manifestOverflow(){
    // Each synthetic contract fits signed 64-bit cents, but their SUM does not.
    const QByteArray big=
       "Rental|R001|Invented|0000000000|C001|2026-10-01|2026-10-02|"
       "|1|50000000000000000.00|50000000000000000.00|0|0\n"
       "Rental|R002|Invented|0000000000|C002|2026-10-01|2026-10-02|"
       "|1|50000000000000000.00|50000000000000000.00|0|0\n";
    Report r;
    if(!fixture(cleanV,big,&r))return false;
    const auto obj=QJsonDocument::fromJson(r.manifest()).object(),
               sums=obj.value("reconciliation").toObject();
    return assertCase(r.has("RECONCILIATION_SUM_OVERFLOW")&&
           obj.value("status")=="BLOCKED"&&
           sums.value("complete")==false &&
           sums.value("declared_booked_cents").isNull() &&
           sums.value("computed_booked_cents").isNull() &&
           sums.value("comparable_contracts").toInt()==1 &&
           obj.value("issues").toArray().size()==1,
           "aggregated int64 overflow must block and null sums");
}
bool encoding(){
    Report a,b,c;
    QByteArray invalid=cleanR;
    invalid.append(char(0xff));
    QByteArray withControls=cleanV;withControls.append('\0');
    QByteArray blank=cleanV+"\n";
    if(!fixture(cleanV,invalid,&a)||!fixture(withControls,cleanR,&b)||
       !fixture(blank,cleanR,&c))return false;
    a.summary();b.summary();c.summary();
    return assertCase(a.has("INVALID_UTF8")&&b.has("FILE_CONTROL")&&
                      c.has("EMPTY_ROW")&&
                      !a.eligible()&&!b.eligible()&&!c.eligible(),
                      "bad UTF-8, NUL, blank row and no write");
}
} // namespace
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=2){qCritical()<<"P7-C requires one fixed fictional test name";return 2;}
    const QString which=QString::fromLatin1(argv[1]);
    bool ok=false;
    if(which=="clean")ok=clean();
    else if(which=="duplicates")ok=duplicates();
    else if(which=="legacy")ok=legacy();
    else if(which=="amounts")ok=amounts();
    else if(which=="malformed")ok=malformed();
    else if(which=="encoding")ok=encoding();
    else if(which=="manifest_clean")ok=manifestClean();
    else if(which=="manifest_blocked")ok=manifestBlocked();
    else if(which=="manifest_incomplete")ok=manifestIncomplete();
    else if(which=="manifest_overflow")ok=manifestOverflow();
    else {qCritical()<<"P7-C invalid fixed case";return 3;}
    if(ok)qInfo()<<"P7-C readonly preflight PASS"<<which;
    return ok?0:1;
}

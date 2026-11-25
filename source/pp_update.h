// ============================================================================
// ACNH Power Pockets - update logic: versions, the GitHub release, the package
// and the installation. No libnx, no SDL, no network here: everything in this
// file is tested on the host (tests/host/test_update.cpp).
//
// What an update can and can't do:
//  - it runs only when the user asks (System > About > Check for updates);
//  - it replaces the files that came in the release zip, in the app's own SD
//    folder, and nothing else: no save, backup, loadout or favorite;
//  - the installation runs when the app closes, after the save was closed, and
//    is undone completely if any step fails or if the console is switched off.
// ============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace pp {

// ------------------------------- versions ----------------------------------
struct Version { int major = 0, minor = 0, patch = 0; bool ok = false; };
Version parseVersion(const std::string& s);              // "1.4.0" or "v1.4.0", nothing else
int compareVersions(const Version& a, const Version& b); // <0, 0, >0

// -------------------------------- SHA-256 ----------------------------------
class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const void* data, size_t n);
    std::string hex();                                   // finishes; call reset() before reuse
private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint64_t total_;
    uint8_t buf_[64];
    size_t used_;
};

// -------------------------------- release ----------------------------------
extern const char* const UPDATE_API_URL;                 // "latest stable release" of this repository
static const size_t UPDATE_JSON_MAX = 256 * 1024;
static const long   UPDATE_ZIP_MAX  = 32L * 1024 * 1024;

struct ReleaseInfo {
    std::string version;                                 // "1.4.1"
    std::string url, sha256;                             // 64 lowercase hex digits
    long size = 0;
    std::vector<std::string> highlights;                 // a few plain-text lines of the release notes
};
enum ReleaseResult { REL_OK, REL_BAD_JSON, REL_NOT_STABLE, REL_BAD_VERSION, REL_NO_PACKAGE, REL_NO_DIGEST };
// Reads the GitHub API answer. The package must have exactly the expected name, size,
// download address (this repository, this tag) and a SHA-256 published by GitHub.
ReleaseResult parseRelease(const std::string& json, ReleaseInfo& out);

// ------------------------------- files (SD) --------------------------------
// Thin layer over stdio so that the tests can make any step fail.
class UpdateFs {
public:
    virtual ~UpdateFs() {}
    virtual FILE* open(const std::string& path, const char* mode);
    virtual bool rename(const std::string& from, const std::string& to);   // `to` must not exist (like libnx)
    virtual bool remove(const std::string& path);        // true when the file is gone
    virtual bool mkdir(const std::string& path);         // true when the folder exists afterwards
    virtual bool exists(const std::string& path);
    virtual bool rmdir(const std::string& path);
    virtual std::vector<std::string> list(const std::string& dir);   // names of files and folders
    // Deletes a folder and what it contains. Only ever called on <app folder>/update/...
    void removeTree(const std::string& dir);
};

// Folders and files of an update, all under the app's SD folder.
struct UpdatePaths {
    explicit UpdatePaths(const std::string& sdDir) : sd(sdDir) {}
    std::string sd;                                      // ends with '/'
    std::string dir() const { return sd + "update"; }
    std::string zip() const { return sd + "update/package.zip"; }
    std::string fresh() const { return sd + "update/new"; }
    std::string old() const { return sd + "update/old"; }
    std::string journal() const { return sd + "update/journal.txt"; }
    std::string done() const { return sd + "update/done.txt"; }
    std::string nro() const { return sd + "acnh_power_pockets.nro"; }
    // Stays next to the app while a swap is not confirmed, so that there is always
    // something to launch from the Homebrew Menu, whenever the power goes out.
    std::string previousNro() const { return sd + "acnh_power_pockets_previous.nro"; }
    std::string keptNro() const { return sd + "update/previous.nro"; }
};

// "switch/acnh_power_pockets/licenses/x.txt" -> rel = "licenses/x.txt". False for anything
// that is not a plain release file: other folders, "..", odd characters, save data (.dat)...
bool allowedEntry(const std::string& zipName, std::string& rel);

enum UnpackResult { UNP_OK, UNP_BAD_ZIP, UNP_BAD_ENTRY, UNP_INCOMPLETE, UNP_WRITE, UNP_CANCELLED };
// Checks and unpacks the package into update/new. `files` = the files to install.
UnpackResult unpackPackage(UpdateFs& fs, const UpdatePaths& p, std::vector<std::string>& files,
                           const std::function<bool()>& cancelled);

enum InstallResult { INST_OK, INST_NOTHING_CHANGED, INST_ROLLED_BACK, INST_BROKEN };
// Swaps the unpacked files in. INST_BROKEN: the rollback itself failed; the next launch
// tries it again (the journal is still there).
InstallResult installUpdate(UpdateFs& fs, const UpdatePaths& p, const std::vector<std::string>& files,
                            const std::string& from, const std::string& to);

enum RecoverResult { REC_NONE, REC_PENDING_CONFIRM, REC_ROLLED_BACK, REC_FAILED };
// At launch, before any file of the app is opened: finishes or undoes an interrupted update.
// runningPrevious: the app was started from acnh_power_pockets_previous.nro.
RecoverResult recoverUpdate(UpdateFs& fs, const UpdatePaths& p, bool runningPrevious);
// Once the app is up and running after an update: forgets the old files (keeps the old .nro).
void confirmUpdate(UpdateFs& fs, const UpdatePaths& p);
// Removes a package that was not installed. Never touches an update being recovered.
void discardDownload(UpdateFs& fs, const UpdatePaths& p);

// -------------------------------- network ----------------------------------
enum NetError { NET_OK, NET_OFFLINE, NET_TLS, NET_HTTP, NET_LIMIT, NET_TOO_BIG, NET_CANCELLED, NET_OTHER };
class NetClient {
public:
    virtual ~NetClient() {}
    // HTTPS only, certificates verified. `sink` returns false to stop.
    virtual NetError get(const std::string& url, size_t maxBytes,
                         const std::function<bool(const uint8_t*, size_t)>& sink,
                         const std::function<bool()>& cancelled) = 0;
};

// ---------------------------------- job ------------------------------------
enum UpdateState { UPD_IDLE, UPD_CHECKING, UPD_UP_TO_DATE, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_PREPARING,
                   UPD_READY, UPD_FAILED, UPD_CANCELLED };
enum UpdateError { UE_NONE, UE_OFFLINE, UE_TLS, UE_HTTP, UE_LIMIT, UE_NETWORK, UE_RELEASE, UE_CHECKSUM, UE_PACKAGE, UE_SD };

// runCheck() / runDownload() block: they run on a worker thread. Everything else may be
// called from the UI thread. release() and files() are only valid once the state says so.
class UpdateJob {
public:
    UpdateJob(NetClient& net, UpdateFs& fs, const std::string& sdDir, const std::string& version)
        : net_(net), fs_(fs), paths_(sdDir), version_(version) {}
    void begin(UpdateState s) { cancel_ = false; percent_ = 0; error_ = UE_NONE; state_.store(s, std::memory_order_release); }
    void runCheck();
    void runDownload();
    void cancel() { cancel_ = true; }
    void abort() { finish(UPD_FAILED, UE_NETWORK); }     // the worker could not be started
    UpdateState state() const { return (UpdateState)state_.load(std::memory_order_acquire); }
    int percent() const { return percent_; }
    UpdateError error() const { return (UpdateError)(int)error_; }
    const ReleaseInfo& release() const { return rel_; }
    const std::vector<std::string>& files() const { return files_; }
private:
    void finish(UpdateState s, UpdateError e = UE_NONE) { error_ = e; state_.store(s, std::memory_order_release); }
    void fail(NetError e);
    NetClient& net_;
    UpdateFs& fs_;
    UpdatePaths paths_;
    std::string version_;
    ReleaseInfo rel_;
    std::vector<std::string> files_;
    std::atomic<int> state_ { UPD_IDLE }, percent_ { 0 }, error_ { UE_NONE };
    std::atomic<bool> cancel_ { false };
};

} // namespace pp

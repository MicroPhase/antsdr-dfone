#include "dfone/session.hpp"

#include "mex.h"

#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr const char *kMexId = "dfone:mex";

using Handle = std::uint64_t;

struct SessionSlot {
    std::unique_ptr<dfone::DfOneSession> session;
};

std::unordered_map<Handle, SessionSlot> &sessions()
{
    static std::unordered_map<Handle, SessionSlot> slots;
    return slots;
}

Handle &next_handle()
{
    static Handle next = 1;
    return next;
}

void cleanup()
{
    sessions().clear();
}

void fail(const char *message)
{
    mexErrMsgIdAndTxt(kMexId, "%s", message);
}

void fail_string(const std::string &message)
{
    mexErrMsgIdAndTxt(kMexId, "%s", message.c_str());
}

std::string get_string(const mxArray *value, const char *name)
{
    mxArray *char_value = nullptr;
    const mxArray *text_value = value;

    if (mxIsClass(value, "string")) {
        mxArray *input = const_cast<mxArray *>(value);
        if (mexCallMATLAB(1, &char_value, 1, &input, "char") != 0 || !char_value) {
            fail_string(std::string("failed to convert ") + name + " to char");
        }
        text_value = char_value;
    }

    if (!mxIsChar(text_value)) {
        if (char_value) {
            mxDestroyArray(char_value);
        }
        fail_string(std::string(name) + " must be a string");
    }

    char *raw = mxArrayToString(text_value);
    if (!raw) {
        if (char_value) {
            mxDestroyArray(char_value);
        }
        fail_string(std::string("failed to read ") + name);
    }

    std::string out(raw);
    mxFree(raw);
    if (char_value) {
        mxDestroyArray(char_value);
    }
    return out;
}

double get_scalar_double(const mxArray *value, const char *name)
{
    if (!mxIsNumeric(value) || mxIsComplex(value) || mxGetNumberOfElements(value) != 1) {
        fail_string(std::string(name) + " must be a real numeric scalar");
    }
    return mxGetScalar(value);
}

std::uint32_t get_u32(const mxArray *value, const char *name)
{
    if (mxIsUint32(value)) {
        return *static_cast<const std::uint32_t *>(mxGetData(value));
    }
    const double parsed = get_scalar_double(value, name);
    if (parsed < 0.0 || parsed > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        fail_string(std::string(name) + " is out of uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint16_t get_u16(const mxArray *value, const char *name)
{
    const std::uint32_t parsed = get_u32(value, name);
    if (parsed > std::numeric_limits<std::uint16_t>::max()) {
        fail_string(std::string(name) + " is out of uint16 range");
    }
    return static_cast<std::uint16_t>(parsed);
}

std::uint64_t get_u64(const mxArray *value, const char *name)
{
    if (mxIsUint64(value)) {
        return *static_cast<const std::uint64_t *>(mxGetData(value));
    }
    const double parsed = get_scalar_double(value, name);
    if (parsed < 0.0 || parsed > 9007199254740992.0) {
        fail_string(std::string(name) + " must be a non-negative integer <= 2^53 unless passed as uint64");
    }
    return static_cast<std::uint64_t>(parsed);
}

std::size_t get_size(const mxArray *value, const char *name)
{
    const std::uint64_t parsed = get_u64(value, name);
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        fail_string(std::string(name) + " is out of size_t range");
    }
    return static_cast<std::size_t>(parsed);
}

bool get_bool(const mxArray *value, const char *name)
{
    if (mxIsLogical(value) && mxGetNumberOfElements(value) == 1) {
        return mxIsLogicalScalarTrue(value);
    }
    return get_scalar_double(value, name) != 0.0;
}

Handle get_handle(const mxArray *value)
{
    const Handle handle = get_u64(value, "handle");
    if (sessions().find(handle) == sessions().end()) {
        fail("invalid or closed DFONE handle");
    }
    return handle;
}

dfone::DfOneSession &get_session(const mxArray *value)
{
    return *sessions().at(get_handle(value)).session;
}

std::int16_t read_i16_le(const std::uint8_t *p)
{
    const auto raw = static_cast<std::uint16_t>(p[0]) |
                     static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
    return static_cast<std::int16_t>(raw);
}

mxArray *make_capture_struct(const dfone::DfOneIqCapture &capture)
{
    const char *fields[] = {
        "iq",
        "frames",
        "sampleRateHz",
        "rxLoHz",
        "channelCount",
        "bytesPerFrame",
        "kind",
        "sampleFormat",
    };
    mxArray *out = mxCreateStructMatrix(1, 1, 8, fields);
    const mwSize frames = static_cast<mwSize>(capture.frames);
    const mwSize channels = static_cast<mwSize>(capture.channel_count);
    mxArray *iq = mxCreateDoubleMatrix(frames, channels, mxCOMPLEX);

    const std::size_t expected = capture.frames * capture.bytes_per_frame;
    if (capture.payload.size() < expected ||
        capture.bytes_per_frame < capture.channel_count * 4U) {
        mxDestroyArray(iq);
        mxDestroyArray(out);
        fail("invalid IQ payload returned by DFONE session");
    }

#if MX_HAS_INTERLEAVED_COMPLEX
    mxComplexDouble *dst = mxGetComplexDoubles(iq);
    for (std::size_t frame = 0; frame < capture.frames; ++frame) {
        const std::uint8_t *base = capture.payload.data() + frame * capture.bytes_per_frame;
        for (std::size_t ch = 0; ch < capture.channel_count; ++ch) {
            const std::size_t idx = frame + ch * capture.frames;
            dst[idx].real = static_cast<double>(read_i16_le(base + ch * 4U));
            dst[idx].imag = static_cast<double>(read_i16_le(base + ch * 4U + 2U));
        }
    }
#else
    double *real = mxGetPr(iq);
    double *imag = mxGetPi(iq);
    for (std::size_t frame = 0; frame < capture.frames; ++frame) {
        const std::uint8_t *base = capture.payload.data() + frame * capture.bytes_per_frame;
        for (std::size_t ch = 0; ch < capture.channel_count; ++ch) {
            const std::size_t idx = frame + ch * capture.frames;
            real[idx] = static_cast<double>(read_i16_le(base + ch * 4U));
            imag[idx] = static_cast<double>(read_i16_le(base + ch * 4U + 2U));
        }
    }
#endif

    mxSetField(out, 0, "iq", iq);
    mxSetField(out, 0, "frames", mxCreateDoubleScalar(static_cast<double>(capture.frames)));
    mxSetField(out, 0, "sampleRateHz", mxCreateDoubleScalar(static_cast<double>(capture.sample_rate_hz)));
    mxSetField(out, 0, "rxLoHz", mxCreateDoubleScalar(static_cast<double>(capture.rx_lo_hz)));
    mxSetField(out, 0, "channelCount", mxCreateDoubleScalar(static_cast<double>(capture.channel_count)));
    mxSetField(out, 0, "bytesPerFrame", mxCreateDoubleScalar(static_cast<double>(capture.bytes_per_frame)));
    mxSetField(out, 0, "kind", mxCreateString(capture.kind == dfone::DfOneIqKind::kUncorrected
                                                  ? "uncorrected"
                                                  : "calibrated"));
    mxSetField(out, 0, "sampleFormat", mxCreateString("CS16_LE_INTERLEAVED_8CH"));
    return out;
}

void require_nrhs(int nrhs, int expected, const char *usage)
{
    if (nrhs != expected) {
        fail(usage);
    }
}

void command_open(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 2 || nrhs > 4) {
        fail("usage: handle = dfone_mex('open', ip, commandPort, dataPort)");
    }
    if (nlhs > 1) {
        fail("open returns one output");
    }

    dfone::DfOneEndpoint endpoint;
    endpoint.device_ip = get_string(prhs[1], "ip");
    if (nrhs >= 3) {
        endpoint.command_port = get_u16(prhs[2], "commandPort");
    }
    if (nrhs >= 4) {
        endpoint.data_port = get_u16(prhs[3], "dataPort");
    }

    auto session = std::make_unique<dfone::DfOneSession>();
    if (!session->connect(endpoint)) {
        fail_string("DFONE connect failed: " + session->last_error());
    }

    mexLock();
    const Handle handle = next_handle()++;
    SessionSlot slot;
    slot.session = std::move(session);
    sessions().emplace(handle, std::move(slot));
    plhs[0] = mxCreateNumericMatrix(1, 1, mxUINT64_CLASS, mxREAL);
    *static_cast<Handle *>(mxGetData(plhs[0])) = handle;
}

void command_close(int nlhs, mxArray *[], int nrhs, const mxArray *prhs[])
{
    require_nrhs(nrhs, 2, "usage: dfone_mex('close', handle)");
    if (nlhs != 0) {
        fail("close returns no outputs");
    }
    const Handle handle = get_u64(prhs[1], "handle");
    auto it = sessions().find(handle);
    if (it != sessions().end()) {
        it->second.session->disconnect();
        sessions().erase(it);
        mexUnlock();
    }
}

void command_is_open(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    require_nrhs(nrhs, 2, "usage: isOpen = dfone_mex('isOpen', handle)");
    if (nlhs > 1) {
        fail("isOpen returns one output");
    }
    const Handle handle = get_u64(prhs[1], "handle");
    const auto it = sessions().find(handle);
    plhs[0] = mxCreateLogicalScalar(it != sessions().end() && it->second.session->connected());
}

void command_configure(int nlhs, mxArray *[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 5 || nrhs > 6) {
        fail("usage: dfone_mex('configure', handle, sampleRateHz, rxLoHz, rxGainDb[, refClock])");
    }
    if (nlhs != 0) {
        fail("configure returns no outputs");
    }

    auto &session = get_session(prhs[1]);
    dfone::DfOneCaptureConfig config;
    config.sample_rate_hz = get_u32(prhs[2], "sampleRateHz");
    config.rx_lo_hz = get_u64(prhs[3], "rxLoHz");
    config.rx_gain_db = get_u32(prhs[4], "rxGainDb");
    if (nrhs >= 6) {
        config.reference_clock =
            static_cast<dfone::DfOneReferenceClock>(get_u32(prhs[5], "refClock") & 0x3U);
    }

    if (!session.configure(config)) {
        fail_string("DFONE configure failed: " + session.last_error());
    }
}

void command_set_u32(int nlhs,
                     mxArray *[],
                     int nrhs,
                     const mxArray *prhs[],
                     const char *usage,
                     bool (dfone::DfOneSession::*setter)(std::uint32_t),
                     const char *label)
{
    require_nrhs(nrhs, 3, usage);
    if (nlhs != 0) {
        fail_string(std::string(label) + " returns no outputs");
    }
    auto &session = get_session(prhs[1]);
    if (!(session.*setter)(get_u32(prhs[2], label))) {
        fail_string(std::string("DFONE ") + label + " failed: " + session.last_error());
    }
}

void command_set_frequency(int nlhs, mxArray *[], int nrhs, const mxArray *prhs[])
{
    require_nrhs(nrhs, 3, "usage: dfone_mex('setFrequency', handle, rxLoHz)");
    if (nlhs != 0) {
        fail("setFrequency returns no outputs");
    }
    auto &session = get_session(prhs[1]);
    if (!session.set_frequency_hz(get_u64(prhs[2], "rxLoHz"))) {
        fail_string("DFONE setFrequency failed: " + session.last_error());
    }
}

void command_set_reference_clock(int nlhs, mxArray *[], int nrhs, const mxArray *prhs[])
{
    require_nrhs(nrhs, 3, "usage: dfone_mex('setReferenceClock', handle, source)");
    if (nlhs != 0) {
        fail("setReferenceClock returns no outputs");
    }
    auto &session = get_session(prhs[1]);
    const auto source =
        static_cast<dfone::DfOneReferenceClock>(get_u32(prhs[2], "source") & 0x3U);
    if (!session.set_reference_clock(source)) {
        fail_string("DFONE setReferenceClock failed: " + session.last_error());
    }
}

void command_capture(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 3 || nrhs > 4) {
        fail("usage: capture = dfone_mex('capture', handle, frames[, uncorrected])");
    }
    if (nlhs > 1) {
        fail("capture returns one output");
    }

    auto &session = get_session(prhs[1]);
    const std::size_t frames = get_size(prhs[2], "frames");
    bool uncorrected = false;
    if (nrhs >= 4) {
        if (mxIsChar(prhs[3]) || mxIsClass(prhs[3], "string")) {
            const std::string kind = get_string(prhs[3], "kind");
            uncorrected = kind == "uncorrected" || kind == "raw";
        } else {
            uncorrected = get_bool(prhs[3], "uncorrected");
        }
    }

    dfone::DfOneIqCapture capture;
    const bool ok = uncorrected
                        ? session.capture_uncorrected_iq(frames, capture)
                        : session.capture_iq(frames, capture);
    if (!ok) {
        fail_string("DFONE capture failed: " + session.last_error());
    }
    plhs[0] = make_capture_struct(capture);
}

}  // namespace

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    if (nrhs < 1) {
        fail("usage: dfone_mex(command, ...)");
    }

    static bool registered_cleanup = false;
    if (!registered_cleanup) {
        mexAtExit(cleanup);
        registered_cleanup = true;
    }

    try {
        const std::string command = get_string(prhs[0], "command");
        if (command == "open") {
            command_open(nlhs, plhs, nrhs, prhs);
        } else if (command == "close") {
            command_close(nlhs, plhs, nrhs, prhs);
        } else if (command == "isOpen") {
            command_is_open(nlhs, plhs, nrhs, prhs);
        } else if (command == "configure") {
            command_configure(nlhs, plhs, nrhs, prhs);
        } else if (command == "setSampleRate") {
            command_set_u32(nlhs,
                            plhs,
                            nrhs,
                            prhs,
                            "usage: dfone_mex('setSampleRate', handle, sampleRateHz)",
                            &dfone::DfOneSession::set_sample_rate_hz,
                            "setSampleRate");
        } else if (command == "setGain") {
            command_set_u32(nlhs,
                            plhs,
                            nrhs,
                            prhs,
                            "usage: dfone_mex('setGain', handle, rxGainDb)",
                            &dfone::DfOneSession::set_gain_db,
                            "setGain");
        } else if (command == "setFrequency") {
            command_set_frequency(nlhs, plhs, nrhs, prhs);
        } else if (command == "setReferenceClock") {
            command_set_reference_clock(nlhs, plhs, nrhs, prhs);
        } else if (command == "capture") {
            command_capture(nlhs, plhs, nrhs, prhs);
        } else {
            fail_string("unknown dfone_mex command: " + command);
        }
    } catch (const std::exception &exc) {
        fail_string(exc.what());
    }
}

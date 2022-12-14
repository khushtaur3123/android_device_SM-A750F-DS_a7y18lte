#include <curl/curl.h>

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <fstream>
#include <memory>

// Tracking Evolution-X's PixelPropsUtils
constexpr const char *UPSTREAM_URL = "https://raw.githubusercontent.com/Evolution-X/frameworks_base/tiramisu/core/java/com/android/internal/util/evolution/PixelPropsUtils.java";

// Use Pixel 7's fingerprint
constexpr const char *DEVICE_CODENAME = "cheetah";
constexpr const char *DEVICE_VENDOR = "google";

constexpr bool STD_STRING_CONTAINS(const std::string& str, const std::string& search) {
	return str.find(search) != std::string::npos;
}
		
struct __attribute__((aligned(64))) result {
	bool vaild = false;
	std::string data;
}; 

using result_t = struct result;

struct __attribute__((aligned(128))) build_data {
	std::string vendor;
	std::string codename;
	int android_version = -1;
	std::string buildid;
	std::string vendordata;
};

static auto WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  static_cast<std::string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

static auto splitNewLines (const std::string& buffer) 
{
    auto result = std::vector<std::string>{};
    auto sstream = std::stringstream{buffer};

    for (std::string line; std::getline(sstream, line, '\n');) {
	    result.push_back(line);
    }

    return result;
}

static auto checkIfVaild(const std::string& line)
{
	auto res = std::make_unique<result_t>();

	// Is it related to <codename>?
	if (!STD_STRING_CONTAINS(line, DEVICE_CODENAME)) {
		return res;
	}

	// Remove non-fingerprint stuff.
	// It should be <vendor>/<codename>/.. format
	if (!STD_STRING_CONTAINS(line, DEVICE_VENDOR)) {
		return res;
	}

	res->vaild = true;
	res->data = line;
	return res;
}

static auto parseJavaFile(const std::string& buffer)
{
	auto kLines = splitNewLines(buffer);
	for (const auto &line : kLines) {
		auto res = checkIfVaild(line);
		if (!res->vaild) {
			res.reset();
		} else {
			std::string data = res->data;
			res->data = data.substr(data.find(DEVICE_VENDOR));
			data = res->data;
			res->data = data.substr(0, data.find_last_of('"'));
			return res;
		}
	}
	// We failed to find the line. return nothing.
	return std::make_unique<result_t>();
}

static auto dirname(const std::string& arg)
{
	auto idx = arg.find_last_of("/\\");
	return arg.substr(0, idx);
}

static auto fromBuildDataToFP(struct build_data *data)
{
	const char *fmt = "%s/%s/%s:%d/%s/%s:user/release-keys";
	const int size = std::snprintf(nullptr, 0, fmt, data->vendor.c_str(), data->codename.c_str(), data->codename.c_str(), data->android_version, data->buildid.c_str(), data->vendordata.c_str());
	std::vector<char> buf(size + 1);
	(void) std::snprintf(buf.data(), buf.size(), fmt, data->vendor.c_str(), data->codename.c_str(), data->codename.c_str(), data->android_version, data->buildid.c_str(), data->vendordata.c_str());

	return std::string(buf.begin(), buf.end());
}

static auto fromBuildDataToDesc(struct build_data *data)
{
	const char *fmt = "%s-user %d %s %s release-keys";
	const int size = std::snprintf(nullptr, 0, fmt, data->codename.c_str(), data->android_version, data->buildid.c_str(), data->vendordata.c_str());
	std::vector<char> buf(size + 1);
	(void) std::snprintf(buf.data(), buf.size(), fmt, data->codename.c_str(), data->android_version, data->buildid.c_str(), data->vendordata.c_str());

	return std::string(buf.begin(), buf.end());
}

static auto serialize(const std::string& fpdata)
{
	auto split = std::vector<std::string>{};
	auto ss = std::stringstream{fpdata};
	auto data = std::make_unique<struct build_data>();
	for (std::string line; std::getline(ss, line, '/');) {
		split.push_back(line);
	}

	// Fingerprint: "<VENDOR>/<CODENAME>/<CODENAME>:<AND_VER>/<BUILDID>/<VENDORDATA>:user/release-keys"
	data->vendor = split[0];
	std::string mixed = split[2];
	auto idx = mixed.find(':');
	data->codename = mixed.substr(0, idx);
	data->android_version = stoi(mixed.substr(idx + 1));
	data->buildid = split[3];
	mixed = split[4];
	idx = mixed.find(':');
	data->vendordata = mixed.substr(0, idx);
	return data;
}

static void apply(const std::string& path, const std::unique_ptr<struct build_data> data)
{
	(void) std::remove(path.c_str());
	std::ofstream file(path);
	if (file.is_open()) {
		std::string str;
		file << "# Autogenerated by " << __FILE__ << std::endl;
		file << "PRODUCT_GMS_CLIENTID_BASE := android-google" << std::endl;
		file << std::endl;
		str = fromBuildDataToFP(data.get());
		str.resize(str.find('\0'));
		file << "BUILD_FINGERPRINT := \"" << str << "\"" << std::endl;
		file << std::endl;
		file << "PRODUCT_BUILD_PROP_OVERRIDES += \\" << std::endl;
		str = fromBuildDataToDesc(data.get());
		str.resize(str.find('\0'));
		file << "\tPRIVATE_BUILD_DESC=\"" << str << "\"" << std::endl;
		file.close();
	}
}

int main(int argc, const char **argv) {
  CURL *curl = nullptr;
  CURLcode res;
  std::string readBuffer;
  const std::string execDir = dirname(argv[0]);

  curl = curl_easy_init();
  if (curl != nullptr) {
    curl_easy_setopt(curl, CURLOPT_URL, UPSTREAM_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != 0u) {
	    return res;
    }
  }

  auto parseRes = parseJavaFile(readBuffer);
  if (!parseRes->vaild) {
	  parseRes.reset();
	  return 1;
  }

  auto build = std::move(serialize(parseRes->data));
  parseRes.reset();
  apply(execDir + "/../fingerprint.mk", std::move(build));
  return 0;
}

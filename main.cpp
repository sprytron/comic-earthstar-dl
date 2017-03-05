#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <chrono>
#include <cstring>
#include <rapidjson/document.h>
#include <curl/curl.h>
#include <cstdlib>
#include <jpeglib.h>

const int magicPatternNumber = 4;
const int tileWidth = 64;
const int tileHeight = 64;

int runningThreads = 0;
std::mutex runningThreadsMutex;
std::mutex coutMutex;

bool quiet = false;

struct ImageInfo
{
	std::string name;	// name to save unscrambled image as
	std::string url;	// scrambled image url
	int pattern;		// used for unscrambling
};

struct PixelData
{
	char* px;
	int width;
	int height;
	int components;
	J_COLOR_SPACE colorSpace;
};

struct TileInfo
{
	int destX;
	int destY;
	int srcX;
	int srcY;
	int width;
	int height;
};

int getPattern(std::string filename)
{
	int charSum = 0;
	for (int i(0); i < filename.length(); ++i)
		charSum += filename[i];
	int pattern = charSum % magicPatternNumber + 1;
	return pattern;
}

int calcPositionWithRest_(int a, int f, int b, int e)
{
	return a * e + (a >= f ? b : 0);
}
int calcXCoordinateXRest_(int a, int f, int b)
{
	return (a + 61 * b) % f;
}
int calcYCoordinateXRest_(int a, int f, int b, int e, int d)
{
	int c = 1 == d % 2;
	(a < f ? c : !c) ? (e = b, f = 0) : (e -= b, f = b);
	return (a + 53 * d + 59 * b) % e + f;
}
int calcXCoordinateYRest_(int a, int f, int b, int e, int d)
{
	int c = 1 == d % 2;
	(a < b ? c : !c) ? (e -= f, b = f) : (e = f, b = 0);
	return (a + 67 * d + f + 71) % e + b;
}
int calcYCoordinateYRest_(int a, int f, int b)
{
	return (a + 73 * b) % f;
}
void getTiles(int a, int f, int b, int e, int d, std::vector<TileInfo> &v)
{
	int c = (a / b);
	int g = (f / e);
	a %= b;
	f %= e;
	int h, l, k, m, p, r, t, q;
	h = c - 43 * d % c;
	h = 0 == h % c ? (c - 4) % c : h;
	h = 0 == h ? c - 1 : h;
	l = g - 47 * d % g;
	l = 0 == l % g ? (g - 4) % g : l;
	l = 0 == l ? g - 1 : l;
	if (0 < a && 0 < f)
	{
		k = h * b;
		m = l * e;
		v.push_back({ k, m, k, m, a, f });
	}
	if (0 < f)
	{
		for (t = 0; t < c; t++)
		{
			p = calcXCoordinateXRest_(t, c, d);
			k = calcYCoordinateXRest_(p, h, l, g, d);
			p = calcPositionWithRest_(p, h, a, b);
			r = k * e;
			k = calcPositionWithRest_(t, h, a, b);
			m = l * e;
			v.push_back({ k, m, p, r, b, f });
		}
	}
	if (0 < a)
	{
		for (q = 0; q < g; q++)
		{
			k = calcYCoordinateYRest_(q, g, d);
			p = calcXCoordinateYRest_(k, h, l, c, d);
			p *= b;
			r = calcPositionWithRest_(k, l, f, e);
			k = h * b;
			m = calcPositionWithRest_(q, l, f, e);
			v.push_back({ k, m, p, r, a, e });
		}
	}
	for (t = 0; t < c; t++)
	{
		for (q = 0; q < g; q++)
		{
			p = (t + 29 * d + 31 * q) % c;
			k = (q + 37 * d + 41 * p) % g;
			r = p >= calcXCoordinateYRest_(k, h, l, c, d) ? a : 0;
			m = k >= calcYCoordinateXRest_(p, h, l, g, d) ? f : 0;
			p = p * b + r;
			r = k * e + m;
			k = t * b + (t >= h ? a : 0);
			m = q * e + (q >= l ? f : 0);
			v.push_back({ k, m, p, r, b, e });
		}
	}
}

size_t write_callback(char *buffer, size_t size, size_t nmemb, void *userp)
{
	std::vector<char> *data = (std::vector<char>*)userp;
	data->insert(data->end(), buffer, buffer + size * nmemb);
	return size * nmemb;
}

// print from multiple threads without interleaved output
// also, don't print if quiet mode
void safe_printline(const std::string &out)
{
	if (!quiet)
	{
		coutMutex.lock();
		std::cout << out << std::endl;
		coutMutex.unlock();
	}
}

void getChapterURL(std::string &url, const std::string cid)
{
	CURL *curl;
	CURLcode res;

	std::vector<char> data;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	std::string jsonURL = "http://api.comic-earthstar.jp/c.php?cid=" + cid;
	curl_easy_setopt(curl, CURLOPT_URL, jsonURL.c_str());

	res = curl_easy_perform(curl);

	if (res == CURLE_OK)
	{
		rapidjson::Document document;
		document.Parse(data.data(), data.size());
		url = document["url"].GetString();
		std::cout << "Chapter URL: " << url << std::endl;
	}
	else
	{
		std::cout << "Failed to get Chapter URL. Error: " << curl_easy_strerror(res) << std::endl;
	}
	curl_easy_cleanup(curl);
}

void getImageInfo(std::vector<ImageInfo> &imgInfo, const std::string &chapterURL)
{
	CURL *curl;
	CURLcode res;

	std::vector<char> data;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	std::string jsonURL = chapterURL + "configuration_pack.json";
	curl_easy_setopt(curl, CURLOPT_URL, jsonURL.c_str());

	res = curl_easy_perform(curl);

	if (res == CURLE_OK)
	{
		std::cout << "Got Chapter JSON: ";
		rapidjson::Document document;
		document.Parse(data.data(), data.size());
		const rapidjson::Value& pageList = document["configuration"]["contents"];

		imgInfo.resize(pageList.Size());
		for (rapidjson::SizeType i = 0; i < pageList.Size(); i++)
		{
			std::string path = pageList[i]["file"].GetString();
			int p1 = path.find_last_of('/') + 1;
			int p2 = path.find_last_of('.');
			imgInfo[i].name = path.substr(p1, p2 - p1) + ".jpg";
			imgInfo[i].url = chapterURL + path + "/0.jpeg";
			imgInfo[i].pattern = getPattern(path + "/0");
		}
		std::cout << imgInfo.size() << " pages." << std::endl;
	}
	else
	{
		std::cout << "Failed to get Chapter JSON. Error: " << curl_easy_strerror(res) << std::endl;
	}
	curl_easy_cleanup(curl);
}

void pxcpy(const PixelData &src, PixelData &dest, int srcX, int srcY, int destX, int destY, int width, int height)
{
	int srcOff = srcY * src.width * src.components + srcX * src.components;
	int destOff = destY * dest.width * dest.components + destX * dest.components;
	int srcInc = src.width * src.components;
	int destInc = dest.width * dest.components;
	int length = width * src.components;
	for (int y(0); y < height; ++y)
	{
		std::memcpy(dest.px + destOff, src.px + srcOff, length);
		destOff += destInc;
		srcOff += srcInc;
	}
}

void jpeg_load(PixelData &image, char* data, int size)
{
	jpeg_decompress_struct cinfo;
	jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);

	jpeg_mem_src(&cinfo, (unsigned char*)data, size);
	jpeg_read_header(&cinfo, TRUE);

	cinfo.out_color_space = cinfo.jpeg_color_space;

	jpeg_start_decompress(&cinfo);

	image.width = cinfo.output_width;
	image.height = cinfo.output_height;
	image.components = cinfo.output_components;
	image.px = new char[cinfo.output_width * cinfo.output_height * cinfo.output_components];
	image.colorSpace = cinfo.out_color_space;
	char* row = image.px;
	while (cinfo.output_scanline < cinfo.output_height)
	{
		unsigned int lines_read = jpeg_read_scanlines(&cinfo, (unsigned char**)&row, cinfo.rec_outbuf_height);
		row += lines_read * cinfo.output_width * cinfo.output_components;
	}
	jpeg_finish_decompress(&cinfo);

	jpeg_destroy_decompress(&cinfo);
}

void jpeg_save(const PixelData &image, const std::string &filename)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	FILE * outfile = fopen(filename.c_str(), "wb");
	if (!outfile)
	{
		safe_printline("\nError: Failed to save " + filename);
	}

	jpeg_stdio_dest(&cinfo, outfile);

	cinfo.image_width = image.width;
	cinfo.image_height = image.height;
	cinfo.input_components = image.components;
	cinfo.in_color_space = image.colorSpace;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 90, TRUE);

	jpeg_start_compress(&cinfo, TRUE);
	char* row = image.px;
	while (cinfo.next_scanline < cinfo.image_height)
	{
		unsigned int lines_written = jpeg_write_scanlines(&cinfo, (unsigned char**)&row, 1);
		row += lines_written * cinfo.image_width * cinfo.input_components;
	}
	jpeg_finish_compress(&cinfo);

	jpeg_destroy_compress(&cinfo);
	fclose(outfile);
}

void saveImage(const ImageInfo &info, const std::string &outDir)
{
	CURL *curl;
	CURLcode res;

	std::vector<char> data;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_URL, info.url.c_str());

	res = curl_easy_perform(curl);
	if (res == CURLE_OK)
	{
		safe_printline("Got " + info.url);

		PixelData in, out;
		jpeg_load(in, data.data(), data.size());
		out = in;
		out.px = new char[in.width * in.height * in.components];

		//std::cout << "Unscrambling... ";
		std::vector<TileInfo> tiles;
		getTiles(in.width, in.height, tileWidth, tileHeight, info.pattern, tiles);

		for (const TileInfo &t : tiles)
			pxcpy(in, out, t.srcX, t.srcY, t.destX, t.destY, t.width, t.height);
		//std::cout << "Saving... ";

		jpeg_save(out, outDir + info.name);
		delete[] in.px;
		delete[] out.px;
		safe_printline("Saved " + info.name);
	}
	else
	{
		safe_printline("Failed to get " + info.url + " Error: " + curl_easy_strerror(res));
	}
	curl_easy_cleanup(curl);
	runningThreadsMutex.lock();
	runningThreads--;
	runningThreadsMutex.unlock();
}

void saveImages(const std::vector<ImageInfo> &imgInfo, const std::string &outDir)
{
	runningThreads = imgInfo.size();
	for (const ImageInfo &info : imgInfo)
	{
		std::thread thread(saveImage, info, outDir);
		thread.detach();
	}
	while (runningThreads > 0)
	{
		std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(3000));
	}
	std::cout << "All Done!" << std::endl;
}

void print_help()
{
	std::cout << "Usage: comic-earthstar-dl CID [options]\n";
	std::cout << "Downloads and unscrambles chapters from comic-earthstar.jp\n";
	std::cout << "Version 0.1.0\n\n";
	std::cout << "CID       The chapter id. You can find this in the viewer URL.\n";
	std::cout << "          Example URL:\n";
	std::cout << "          http://viewer.comic-earthstar.jp/\n";
	std::cout << "          viewer.html?cid=ede7e2b6d13a41ddf9f4bdef84fdc737&cty=1&lin=0\n";
	std::cout << "          The CID would be: ede7e2b6d13a41ddf9f4bdef84fdc737\n";
	std::cout << "Options:\n";
	std::cout << "-o <dir>  Specify output directory. It should already exist.\n";
	std::cout << "          Default is current dirrectory.\n";
	//std::cout << "-t <num>  Specify number of threads to use.\n";
	//std::cout << "          Default is 6.\n";
	std::cout << "-q        Quiet. Hides most of the messages.\n";
}

int main(int argc, char** argv)
{
	std::string cid, outDir("./");
	//int threads = std::thread::hardware_concurrency();
	bool argsOk(false);

	for (int i(1); i < argc; ++i)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
			case 'o':
				outDir = argv[++i];
				if (outDir.back() != '/' &&outDir.back() != '\\')
					outDir += '/';
				break;
			case 'q':
				quiet = true;
				break;
				//case 't':
				//	threads = std::stoi(argv[++i]);
				//	break;
			}
		}
		else
		{
			cid = argv[i];
			argsOk = true;
		}
	}
	if (!argsOk)
	{
		print_help();
		return 1;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	std::vector<ImageInfo> imgInfo;
	std::string chapterURL;
	getChapterURL(chapterURL, cid);
	getImageInfo(imgInfo, chapterURL);
	saveImages(imgInfo, outDir);

	curl_global_cleanup();
	return 0;
}
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <Windows.h>

#include <ffms.h>

std::string convert_to_utf8(const wchar_t *in)
{
	int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in, -1, nullptr, 0, nullptr, false);
	if (len == 0 && GetLastError() != NO_ERROR)
	{
		throw std::runtime_error("[utf-8] failed to determine length");
	}
	std::string s(len, 0);
	int rv = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, in, -1, &s[0], len, nullptr, false);
	if (len == 0 && GetLastError() != NO_ERROR)
	{
		throw std::runtime_error("[utf-8] conversion failed");
	}
	return s;
}

int FFMS_CC indexer_callback(int64_t current, int64_t total, void *)
{
	static double last = 0;
	double progress = static_cast<double>(current) / total*100.0;
	// The indexer callback slows down the indexing lol
	if (progress - last > 10 || progress >= 100)
	{
		last = progress;
		printf("Indexing: %.02f%%\r", progress);
	}
	return 0;
}

std::vector<double> get_timecodes(const std::string& in)
{
	char errmsg[1024];
	FFMS_ErrorInfo errinfo;
	errinfo.Buffer = errmsg;
	errinfo.BufferSize = sizeof(errmsg);
	errinfo.ErrorType = FFMS_ERROR_SUCCESS;
	errinfo.SubType = FFMS_ERROR_SUCCESS;

	FFMS_Indexer *indexer = FFMS_CreateIndexer(in.c_str(), &errinfo);
	if (indexer == NULL) {
		throw std::runtime_error(errinfo.Buffer);
	}

	FFMS_SetProgressCallback(indexer, indexer_callback, nullptr);

	FFMS_Index *index = FFMS_DoIndexing2(indexer, FFMS_IEH_ABORT, &errinfo);
	if (index == NULL) {
		throw std::runtime_error(errinfo.Buffer);
	}
	indexer_callback(1, 1, nullptr);

	int trackno = FFMS_GetFirstTrackOfType(index, FFMS_TYPE_VIDEO, &errinfo);
	if (trackno < 0) {
		throw std::runtime_error("no video tracks found");
	}

	FFMS_Track *track = FFMS_GetTrackFromIndex(index, trackno);
	const FFMS_TrackTimeBase* tb = FFMS_GetTimeBase(track);
	int numFrames = FFMS_GetNumFrames(track);
	std::vector<double> timestamps(numFrames);

	for (int i = 0; i < numFrames; ++i)
	{
		const auto frameInfo = FFMS_GetFrameInfo(track, i);
		timestamps[i] = (frameInfo->PTS * tb->Num) / static_cast<double>(tb->Den);
	}

	FFMS_DestroyIndex(index);
	return timestamps;
}

void write_timecodes(const std::wstring& out, const std::vector<double>& tc)
{
	FILE *fp = nullptr;
	if (_wfopen_s(&fp, out.c_str(), L"wb"))
	{
		throw std::runtime_error("failed to open output file");
	}

	double offset = tc.size() > 0 ? -tc[0] : 0;
	wprintf(L"\nWriting to %s, offset: %.06f, frames: %zd\n", out.c_str(), offset, tc.size());
	fprintf(fp, "# timecode format v2\n");

	for (double v : tc)
	{
		fprintf(fp, "%.06f\n", v + offset);
	}
	
	fclose(fp);
}

int wmain(int argc, wchar_t *argv[])
{
	FFMS_Init(0, 0);
	if (argc < 2 || argc > 3 || !wcscmp(argv[1], L"-h"))
	{
		wprintf(L"Usage: %s input [timecodes.txt]\n", argv[0]);
		return 1;
	}

	const std::string& in = convert_to_utf8(argv[1]);
	std::wstring out;
	if (argc <= 2)
	{
		out = argv[1];
		size_t idx = out.find_last_of(L".");
		if (idx != std::wstring::npos)
		{
			out = out.substr(0, idx);
		}
		out += L"_timecodes.txt";
	}
	else
	{
		out = argv[2];
	}

	write_timecodes(out, get_timecodes(in));
	return 0;
}
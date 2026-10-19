// render_events_tests.cpp - src/shared/render_events.h: the one-line events the renderer prints for the GUI.
#include <gtest/gtest.h>

#include "../../src/shared/render_events.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using render_events::Event;

TEST(RenderEventsTest, AnEventIsOneLineThatStartsWithThePrefix) {
	Event e = render_events::errorEvent(3, "scene-file-missing", "The scene's files are not on this computer.");
	e.add("file", "pbrt_scenes/bitterli/spaceship/scene-v4.pbrt");
	const std::string line = render_events::toLine(e);
	EXPECT_EQ(line.rfind("@rt {", 0), 0u);
	EXPECT_EQ(line.find('\n'), std::string::npos);
	EXPECT_EQ(line, "@rt {\"type\":\"error\",\"code\":\"3\",\"reason\":\"scene-file-missing\",\"message\":\"The scene's files are not on this computer.\","
	                "\"file\":\"pbrt_scenes/bitterli/spaceship/scene-v4.pbrt\"}");
}

TEST(RenderEventsTest, ParsingGivesBackWhatWasWritten) {
	Event e("error");
	e.add("code", "211").add("reason", "scene-not-supported-on-gpu").add("scene", "H35");
	Event back;
	ASSERT_TRUE(render_events::parseLine(render_events::toLine(e), back));
	EXPECT_EQ(back.type, "error");
	EXPECT_EQ(back.get("code"), "211");
	EXPECT_EQ(back.get("reason"), "scene-not-supported-on-gpu");
	EXPECT_EQ(back.get("scene"), "H35");
	EXPECT_FALSE(back.has("nothing"));
	EXPECT_EQ(back.get("nothing"), "");
}

TEST(RenderEventsTest, QuotesBackslashesControlCharactersAndUtf8SurviveTheRoundTrip) {
	Event e("error");
	e.add("file", "C:\\Users\\name\\a \"quoted\" file.pbrt")
	 .add("detail", std::string("line one\nline two\ttab\x01 and \xC3\xA9 and \xE6\x97\xA5"));
	const std::string line = render_events::toLine(e);
	EXPECT_EQ(line.find('\n'), std::string::npos) << "a newline in a value is escaped, so the event stays one line";
	Event back;
	ASSERT_TRUE(render_events::parseLine(line, back));
	EXPECT_EQ(back.get("file"), "C:\\Users\\name\\a \"quoted\" file.pbrt");
	EXPECT_EQ(back.get("detail"), std::string("line one\nline two\ttab\x01 and \xC3\xA9 and \xE6\x97\xA5"));
}

TEST(RenderEventsTest, ALineThatIsNotAnEventIsLeftAlone) {
	Event e;
	EXPECT_FALSE(render_events::parseLine("[OptiX] Scene not supported on GPU", e));
	EXPECT_FALSE(render_events::parseLine("", e));
	EXPECT_FALSE(render_events::parseLine("@rt", e));
	EXPECT_FALSE(render_events::parseLine("@rt {", e));
	EXPECT_FALSE(render_events::parseLine("@rt {}", e)) << "no type";
	EXPECT_FALSE(render_events::parseLine("@rt {\"code\":\"3\"}", e)) << "no type";
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":\"error\"", e)) << "not closed";
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":\"error\",}", e));
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":3}", e)) << "values are strings";
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":\"error\"} trailing", e));
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":\"unterminated}", e));
	EXPECT_FALSE(render_events::parseLine("@rt {\"type\":\"bad\\q\"}", e)) << "unknown escape";
}

TEST(RenderEventsTest, ATypeOnlyEventAndWhitespaceBetweenTokensParse) {
	Event e;
	ASSERT_TRUE(render_events::parseLine("@rt {\"type\":\"progress\"}", e));
	EXPECT_EQ(e.type, "progress");
	EXPECT_TRUE(e.fields.empty());
	ASSERT_TRUE(render_events::parseLine("@rt { \"type\" : \"error\" , \"code\" : \"3\" }", e));
	EXPECT_EQ(e.get("code"), "3");
}

TEST(RenderEventsTest, AMissingSceneFileGivesAnEventNamingTheFileAndTheFolder) {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "rt_events_test";
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
	const std::string scenePath = (dir / "not-downloaded" / "scene.pbrt").string();

	std::ostringstream captured;
	std::streambuf* old = std::cout.rdbuf(captured.rdbuf());
	const bool missing = render_events::emitSceneFilesMissing(3, "H35", scenePath);
	std::cout.rdbuf(old);

	ASSERT_TRUE(missing);
	std::istringstream lines(captured.str());
	Event e;
	bool found = false;
	for (std::string line; std::getline(lines, line);)
		if (render_events::parseLine(line, e)) found = true;
	ASSERT_TRUE(found) << captured.str();
	EXPECT_EQ(e.type, "error");
	EXPECT_EQ(e.get("code"), "3");
	EXPECT_EQ(e.get("reason"), "scene-file-missing");
	EXPECT_EQ(e.get("scene"), "H35");
	EXPECT_NE(e.get("file").find("scene.pbrt"), std::string::npos);
	EXPECT_FALSE(e.get("folder").empty());
	EXPECT_NE(e.get("message").find("Download missing files"), std::string::npos);

	{
		std::filesystem::create_directories(dir / "not-downloaded", ec);
		std::ofstream out(scenePath);
		out << "WorldBegin\n";
	}
	std::ostringstream quiet;
	old = std::cout.rdbuf(quiet.rdbuf());
	const bool stillMissing = render_events::emitSceneFilesMissing(3, "H35", scenePath);
	std::cout.rdbuf(old);
	EXPECT_FALSE(stillMissing) << "a scene whose files are all there prints nothing";
	EXPECT_TRUE(quiet.str().empty());
	fs::remove_all(dir, ec);
}

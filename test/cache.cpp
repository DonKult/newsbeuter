#include "catch.hpp"
#include "test-helpers.h"

#include <cache.h>
#include <configcontainer.h>
#include <rss_parser.h>

using namespace newsbeuter;

TEST_CASE("cache behaves correctly") {
	TestHelpers::TempFile dbfile;
	configcontainer * cfg = new configcontainer();
	cache * rsscache = new cache(dbfile.getPath(), cfg);
	rss_parser parser("file://data/rss.xml", rsscache, cfg, nullptr);
	std::shared_ptr<rss_feed> feed = parser.parse();
	REQUIRE(feed->total_item_count() == 8);
	rsscache->externalize_rssfeed(feed, false);

	SECTION("items in search result are marked as read") {
		auto search_items = rsscache->search_for_items("Botox", "");
		REQUIRE(search_items.size() == 1);
		auto item = search_items.front();
		REQUIRE(true == item->unread());

		item->set_unread(false);
		search_items.clear();

		search_items = rsscache->search_for_items("Botox", "");
		REQUIRE(search_items.size() == 1);
		auto updatedItem = search_items.front();
		REQUIRE(false == updatedItem->unread());
	}

	std::vector<std::shared_ptr<rss_feed>> feedv;
	feedv.push_back(feed);

	cfg->set_configvalue("cleanup-on-quit", "true");
	rsscache->cleanup_cache(feedv);

	delete rsscache;
	delete cfg;
}

TEST_CASE("Cleaning old articles works") {
	/* Populating the cache file with some items. */
	configcontainer * cfg = nullptr;
	cache * rsscache = nullptr;
	TestHelpers::TempFile dbfile;

	cfg = new configcontainer();
	rsscache = new cache(dbfile.getPath(), cfg);
	rss_parser * parser = new rss_parser(
			"file://data/rss.xml", rsscache, cfg, nullptr);
	std::shared_ptr<rss_feed> feed = parser->parse();

	/* Adding a fresh item that won't be deleted. If it survives the test, we
	 * will know that "keep-articles-days" really deletes the old articles
	 * *only* and not the whole database. */
	auto item = std::make_shared<rss_item>(rsscache);
	item->set_title("Test item");
	item->set_link("http://example.com/item");
	item->set_guid("http://example.com/item");
	item->set_author("Newsbeuter Testsuite");
	item->set_description("");
	item->set_pubDate(time(nullptr)); // current time
	item->set_unread(true);
	feed->add_item(item);

	rsscache->externalize_rssfeed(feed, false);

	/* Simulating a restart of Newsbeuter. */
	delete parser;
	delete rsscache;
	delete cfg;

	/* Setting "keep-articles-days" to non-zero value to trigger
	 * cache::clean_old_articles().
	 *
	 * The value of 42 days is sufficient because the items in the test feed
	 * are dating back to 2006. */
	cfg = new configcontainer();
	cfg->set_configvalue("keep-articles-days", "42");
	rsscache = new cache(dbfile.getPath(), cfg);
	rss_ignores * ign = new rss_ignores();
	feed = rsscache->internalize_rssfeed("file://data/rss.xml", ign);

	/* The important part: old articles should be gone, new one remains. */
	REQUIRE(feed->items().size() == 1);

	/* Cleanup. */
	delete ign;
	delete rsscache;
	delete cfg;
}

TEST_CASE("Last-Modified and ETag values are preserved correctly") {
	configcontainer * cfg = new configcontainer();
	cache * rsscache = new cache(":memory:", cfg);
	const auto feedurl = "file://data/rss.xml";
	rss_parser parser(feedurl, rsscache, cfg, nullptr);
	std::shared_ptr<rss_feed> feed = parser.parse();
	rsscache->externalize_rssfeed(feed, false);

	/* We will run this lambda on different inputs to check different
	 * situations. */
	auto test = [&](const time_t& lm_value, const std::string& etag_value) {
		time_t last_modified = lm_value;
		std::string etag = etag_value;

		rsscache->update_lastmodified(feedurl, last_modified, etag);
		/* Scrambling the value to make sure the following call changes it. */
		last_modified = 42;
		etag = "42";
		rsscache->fetch_lastmodified(feedurl, last_modified, etag);

		REQUIRE(last_modified == lm_value);
		REQUIRE(etag == etag_value);
	};

	SECTION("Only Last-Modified header was returned") {
		test(1476382350, "");
	}

	SECTION("Only ETag header was returned") {
		test(0, "1234567890");
	}

	SECTION("Both Last-Modified and ETag headers were returned") {
		test(1476382350, "1234567890");
	}

	delete rsscache;
	delete cfg;
}

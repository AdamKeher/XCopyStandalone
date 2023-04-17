// ArduinoJson - https://arduinojson.org
// Copyright © 2014-2022, Benoit BLANCHON
// MIT License

#include <ArduinoJson.h>
#include <catch.hpp>

TEST_CASE("JsonVariant::size()") {
  DynamicJsonDocument doc(4096);
  JsonVariant variant = doc.to<JsonVariant>();

  SECTION("unbound reference") {
    JsonVariant unbound;

    CHECK(unbound.size() == 0);
  }

  SECTION("int") {
    variant.set(42);

    CHECK(variant.size() == 0);
  }

  SECTION("string") {
    variant.set("hello");

    CHECK(variant.size() == 0);
  }

  SECTION("object") {
    variant["a"] = 1;
    variant["b"] = 2;

    CHECK(variant.size() == 2);
  }

  SECTION("linked object") {
    StaticJsonDocument<1024> doc2;
    doc2["hello"] = "world";
    variant.link(doc2);

    CHECK(variant.size() == 1);
  }

  SECTION("linked array") {
    StaticJsonDocument<1024> doc2;
    doc2.add(1);
    doc2.add(2);
    variant.link(doc2);

    CHECK(variant.size() == 2);
  }
}

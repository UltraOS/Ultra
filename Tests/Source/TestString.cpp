#include "TestRunner.h"

#include "Common/String.h"

// Should be SSO for x86 & x86-64
static const char* sso_string = "SSO St";

// Shouldn't be SSO
static const char* non_sso_string = "NON SSO String!!!!!!!!!Hello";

TEST(Basic) {
    kernel::String test;

    test = sso_string;

    Assert::that(test.c_string()).is_equal(sso_string);

    test = non_sso_string;

    Assert::that(test.c_string()).is_equal(non_sso_string);
}

TEST(Append) {
    kernel::String test;

    Assert::that(test.c_string()).is_equal("");

    test.append('H');
    Assert::that(test.c_string()).is_equal("H");
    test.append("E");
    Assert::that(test.c_string()).is_equal("HE");
    test.append("L");
    Assert::that(test.c_string()).is_equal("HEL");
    test.append("LO");
    Assert::that(test.c_string()).is_equal("HELLO");

    test << " WORLD " << kernel::format::as_hex << 0xDEADBEEF << kernel::format::as_dec << 123;

    Assert::that(test.c_string()).is_equal("HELLO WORLD 0xDEADBEEF123");
}

TEST(CopyOwnership) {
    kernel::String test = non_sso_string;
    kernel::String test1 = test;

    Assert::that(test1.c_string()).is_equal(test.c_string());
    Assert::that(test1.size()).is_equal(test.size());

    kernel::String test2 = sso_string;
    kernel::String test3 = test2;

    Assert::that(test3.c_string()).is_equal(test2.c_string());
    Assert::that(test3.size()).is_equal(test2.size());
}

TEST(MoveOwnership) {
    kernel::String test = non_sso_string;
    kernel::String test1 = move(test);

    Assert::that(test.c_string()).is_equal("");
    Assert::that(test.size()).is_equal(0);

    Assert::that(test1.c_string()).is_equal(non_sso_string);
    Assert::that(test1.size()).is_equal(kernel::length_of(non_sso_string));

    kernel::String test2 = sso_string;
    kernel::String test3 = move(test2);

    Assert::that(test2.c_string()).is_equal("");
    Assert::that(test2.size()).is_equal(0);

    Assert::that(test3.c_string()).is_equal(sso_string);
    Assert::that(test3.size()).is_equal(kernel::length_of(sso_string));
}

TEST(PopBackSSO) {
    kernel::String sso_test = "sso";

    sso_test.pop_back();
    Assert::that(sso_test.c_string()).is_equal("ss");
    Assert::that(sso_test.size()).is_equal(2);

    sso_test.pop_back();
    Assert::that(sso_test.c_string()).is_equal("s");
    Assert::that(sso_test.size()).is_equal(1);
}

TEST(PopBackNonSSO) {
    kernel::String non_sso_test = "non-sso-string-that-is-long";
    size_t initial_length = non_sso_test.size();

    non_sso_test.pop_back();
    Assert::that(non_sso_test.c_string()).is_equal("non-sso-string-that-is-lon");
    Assert::that(non_sso_test.size()).is_equal(initial_length - 1);

    for (size_t i = 0; i < initial_length - 4; ++i) {
        non_sso_test.pop_back();
    }

    Assert::that(non_sso_test.c_string()).is_equal("non");
    Assert::that(non_sso_test.size()).is_equal(3);
}

TEST(StackString) {
    kernel::StackString builder;
    builder += 123;
    builder += "456";
    builder << kernel::format::as_hex << 0xDEADBEEF;
    builder.append("test");
    builder.append(true);
    builder.append('!');

    Assert::that(const_cast<const char*>(builder.data())).is_equal("1234560xDEADBEEFtesttrue!");
}

TEST(StackStringLeftShift) {
    kernel::StackString builder;

    int* my_pointer = reinterpret_cast<int*>(static_cast<decltype(sizeof(int))>(0xDEADBEEF));

    builder << 123 << "456" << my_pointer << "test" << true << '!';

    const char* expected_string = nullptr;

    if constexpr (sizeof(void*) == 8) {
        expected_string = "1234560x00000000DEADBEEFtesttrue!";
    } else {
        expected_string = "1234560xDEADBEEFtesttrue!";
    }

    Assert::that(const_cast<const char*>(builder.data())).is_equal(expected_string);
}

TEST(StartsWith) {
    kernel::StringView str1 = "TEST";

    kernel::StringView str2 = "TEST123321";
    kernel::StringView str3 = "";
    kernel::StringView str4 = "TES123123112312321";
    kernel::StringView str5 = "TEST";

    Assert::that(str2.starts_with(str1)).is_true();
    Assert::that(str3.starts_with(str1)).is_false();
    Assert::that(str1.starts_with(str3)).is_true();
    Assert::that(str4.starts_with(str1)).is_false();
    Assert::that(str5.starts_with(str1)).is_true();
}

TEST(EndsWith) {
    kernel::StringView str1 = "TEST";

    kernel::StringView str2 = "123321TEST";
    kernel::StringView str3 = "";
    kernel::StringView str4 = "123123112312321EST";
    kernel::StringView str5 = "TEST";

    Assert::that(str2.ends_with(str1)).is_true();
    Assert::that(str3.ends_with(str1)).is_false();
    Assert::that(str1.ends_with(str3)).is_true();
    Assert::that(str4.ends_with(str1)).is_false();
    Assert::that(str5.ends_with(str1)).is_true();
}

TEST(Find) {
    kernel::StringView str1 = "TEST";

    kernel::StringView str2 = "TEST123321";
    kernel::StringView str3 = "";
    kernel::StringView str4 = "TES1231TEST12312321";
    kernel::StringView str5 = "TEASTXXXXXXXTEST";

    Assert::that(str2.find(str1).value_or(-1)).is_equal(0);
    Assert::that(str3.find(str1).value_or(-1)).is_equal(-1);
    Assert::that(str1.find(str3).value_or(-1)).is_equal(0);
    Assert::that(str4.find(str1).value_or(-1)).is_equal(7);
    Assert::that(str5.find(str1).value_or(-1)).is_equal(12);
    Assert::that(str1.find(str1).value_or(-1)).is_equal(0);
}

TEST(FindLast) {
    kernel::StringView str1 = ".....";
    kernel::StringView str2 = "";
    kernel::StringView str3 = ".";
    kernel::StringView str4 = "HeLLo";
    kernel::StringView str5 = "CLoSeASD";
    kernel::StringView str6 = "CLoSeRASD";

    Assert::that(str1.find_last(str3).value_or(-1)).is_equal(str1.size() - 1);
    Assert::that(str1.find_last(str2).value_or(-1)).is_equal(str1.size());
    Assert::that(str3.find_last(str1).value_or(-1)).is_equal(-1);
    Assert::that(str4.find_last(str4).value_or(-1)).is_equal(0);
    Assert::that(str2.find_last(str2).value_or(-1)).is_equal(0);
    Assert::that(str5.find_last("ASD").value_or(-1)).is_equal(5);
    Assert::that(str6.find_last("ASD").value_or(-1)).is_equal(6);
    Assert::that(str5.find_last("CLoSeR").value_or(-1)).is_equal(-1);
    Assert::that(str6.find_last("CLoSeR").value_or(-1)).is_equal(0);
}

TEST(Strip) {
    kernel::String str1 = "ASD";
    kernel::String str2 = " A  ";
    kernel::String str3 = "  X";
    kernel::String str4 = "X  ";
    kernel::String str5 = "   XXX   Y   ";
    kernel::String str6 = "                              ASD                                     ";
    kernel::String str7 = "   ";
    kernel::String str8 = "   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA  ";

    str1.strip();
    Assert::that(str1).is_equal("ASD");

    str2.strip();
    Assert::that(str2).is_equal("A");

    str3.strip();
    Assert::that(str3).is_equal("X");

    str4.strip();
    Assert::that(str4).is_equal("X");

    str5.strip();
    Assert::that(str5).is_equal("XXX   Y");

    str6.strip();
    Assert::that(str6).is_equal("ASD");

    str7.strip();
    Assert::that(str7).is_equal("");
    Assert::that(str7.empty()).is_true();

    str8.strip();
    Assert::that(str8).is_equal("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
}

TEST(Rstrip) {
    kernel::String str1 = "";
    kernel::String str2 = " A  ";
    kernel::String str3 = "  X........";
    kernel::String str4 = "X..";
    kernel::String str5 = ".....XXX   Y... ";
    kernel::String str6 = "...";

    str1.rstrip();
    Assert::that(str1).is_equal("");

    str2.rstrip();
    Assert::that(str2).is_equal(" A");

    str3.rstrip('.');
    Assert::that(str3).is_equal("  X");

    str4.rstrip('.');
    Assert::that(str4).is_equal("X");

    str5.rstrip('.');
    Assert::that(str5).is_equal(".....XXX   Y... ");

    str6.rstrip('.');
    Assert::that(str6).is_equal("");
}

TEST(ToLower) {
    kernel::String str1 = "lower_str!@#$%MZAxerfcSD";
    kernel::String str2 = "";
    kernel::String str3 = "ASDDSSMVPE!$123456ABCD69";
    kernel::String str4 = "A";
    kernel::String str5 = "a";

    str1.to_lower();
    Assert::that(str1).is_equal("lower_str!@#$%mzaxerfcsd");

    str2.to_lower();
    Assert::that(str2).is_equal("");

    str3.to_lower();
    Assert::that(str3).is_equal("asddssmvpe!$123456abcd69");

    str4.to_lower();
    Assert::that(str4).is_equal("a");

    str5.to_lower();
    Assert::that(str5).is_equal("a");
}

TEST(ToUpper) {
    kernel::String str1 = "lower_str!@#$%MZAxerfcSD";
    kernel::String str2 = "";
    kernel::String str3 = "Asddldfg043MVpE!$123456ABcD69";
    kernel::String str4 = "A";
    kernel::String str5 = "a";

    str1.to_upper();
    Assert::that(str1).is_equal("LOWER_STR!@#$%MZAXERFCSD");

    str2.to_upper();
    Assert::that(str2).is_equal("");

    str3.to_upper();
    Assert::that(str3).is_equal("ASDDLDFG043MVPE!$123456ABCD69");

    str4.to_upper();
    Assert::that(str4).is_equal("A");

    str5.to_upper();
    Assert::that(str5).is_equal("A");
}

TEST(CaseInsensitiveCompare) {
    kernel::StringView str1 = "helloWorld";
    kernel::StringView str2 = "HELLOwORLd";
    kernel::StringView str3 = "";

    Assert::that(str1.case_insensitive_equals(str2)).is_true();
    Assert::that(str2.case_insensitive_equals(str3)).is_false();
    Assert::that(str1.case_insensitive_equals(str1)).is_true();
    Assert::that(str2.case_insensitive_equals(str2)).is_true();
}

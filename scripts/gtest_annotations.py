#!/usr/bin/env python3
"""Prints each failed test of gtest XML reports (--gtest_output=xml:FILE) as a GitHub Actions error annotation.

A red CI job's log needs signing in to read, but its annotations do not: with this run after the tests (`if: failure()`), the run's page and the
public API say which tests failed and why, instead of only "Process completed with exit code 1". A report that is missing (its tests did not run, or crashed or
hung before writing it) is said too - the test that was running is then the last one in the job's log.

usage: python scripts/gtest_annotations.py REPORT.xml [REPORT.xml ...]
"""
import os
import sys
import xml.etree.ElementTree as ET


def escape(text):
    return text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")


def main(paths):
    failed = 0
    for path in paths:
        if not os.path.exists(path):
            print("::error title=No test report::%s was not written: the tests did not run (a step before them failed), or crashed, hung or were stopped" % path)
            failed += 1
            continue
        for case in ET.parse(path).getroot().iter("testcase"):
            for failure in case.findall("failure"):
                name = "%s.%s" % (case.get("classname"), case.get("name"))
                message = (failure.get("message") or failure.text or "").strip()
                print("::error title=%s::%s" % (escape(name), escape(message[:3000])))
                failed += 1
    print("%d failed test(s) in %d report(s)" % (failed, len(paths)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

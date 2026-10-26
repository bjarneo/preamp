#pragma once

#include <QStringList>

// Commands that need no display. Each prints what it did, or the error on
// stderr, and returns the exit code. `--json` prints the daemon's answer.
namespace cli {

bool handles(const QString &command);
int run(const QStringList &args);
int runDaemon();
void printHelp();

} // namespace cli

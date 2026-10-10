
import os, time
import subprocess
import argparse


class Watchdog(object):
    SLEEP = .3
    def __init__(self, command, files):
        self.watch = {}
        self.command = command
        self.prepare_files(files)

    def prepare_files(self, files):
        if "." in files:
            files.remove(".")
            files += [f for f in os.listdir(os.getcwd()) if not f.startswith(".")]
        files = set(files)
        self.watch = dict.fromkeys(files)
        for file in files:
            if not os.path.exists(file):
                raise IOError(f"File \"{file}\" not found")
        for file_key in self.watch.keys():
            self.watch[file_key] = os.stat(file_key).st_mtime

    def start(self):
        changed = False
        while True:
            for file in self.watch.keys():
                act = os.stat(file).st_mtime
                if not self.watch[file] == act:
                    changed = file
                    self.watch[file] = act
            if changed:
                print(f"File \"{changed}\" changed since last check")
                changed = False
                subprocess.call(self.command)
            time.sleep(self.SLEEP)

if __name__ == '__main__':
    print("Watchdog started...")
    parser = argparse.ArgumentParser(description="Watchdog")
    parser.add_argument("-w", "--watch", nargs="+", required=True, help="Files to watch")
    parser.add_argument("-c", "--command", nargs="+",required=True, help="Command to run")
    args = parser.parse_args()
    watchdog = Watchdog(args.command, args.watch)
    try:
        watchdog.start()
    except KeyboardInterrupt:
        print()

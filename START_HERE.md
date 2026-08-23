# START HERE — Beginner Guide for the Assignment

This guide explains what to do from the very beginning. Read it once before running any commands.

## 1. First Understand What You Are Building

You are not building a physical car CAN network.

You are creating a **software simulation of a vehicle CAN network** on Linux.

The basic idea is:

```text
C transmitter
     |
     | CAN frames
     v
  vcan0
     |
     +--------------------+
     |                    |
     v                    v
candump             C dashboard
     |
     |                    |
     +---------> SavvyCAN
```

The transmitter pretends to be an ECU. It creates values such as speed, RPM, coolant temperature, fuel, battery voltage, and ambient temperature.

The DBC explains what those numbers mean inside the CAN bytes.

The dashboard acts like a diagnostic/telemetry receiver.

## 2. What Each File Does

### `vehicle.dbc`

This is the CAN database.

It tells software:

- which CAN IDs exist;
- what each message is called;
- where every signal starts;
- how many bits each signal uses;
- what factor and offset are required for decoding;
- what physical range and unit each signal uses.

### `can_transmitter.c`

This program:

1. connects to `vcan0`;
2. generates moving vehicle values;
3. converts physical values to raw integers;
4. packs raw values into CAN bits;
5. sends IDs `0x100`, `0x101`, and `0x102`.

### `can_dashboard.c`

This program:

1. connects to `vcan0`;
2. waits for CAN frames;
3. identifies the CAN ID;
4. extracts the correct signal bits;
5. converts raw integers back into engineering units;
6. prints the current values.

### `AI_USAGE_REPORT.md`

This explains the AI-assisted workflow and, more importantly, the human checks that prevent incorrect AI-generated CAN logic from being accepted blindly.

### `README.md`

This is the technical documentation and setup guide that can be shown to your instructor or placed on GitHub.

## 3. Install Linux Tools

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential can-utils iproute2 git
```

Confirm GCC:

```bash
gcc --version
```

Confirm Git:

```bash
git --version
```

Confirm `candump`:

```bash
candump --help | head
```

## 4. Open the Friend's Project Folder

After downloading/extracting this repository, enter the folder:

```bash
cd AI-Assisted-DBC-SocketCAN-Version-B
```

Check the files:

```bash
ls
```

You should see:

```text
AI_USAGE_REPORT.md
Makefile
README.md
START_HERE.md
can_dashboard.c
can_transmitter.c
vehicle.dbc
.gitignore
```

## 5. Create `vcan0`

Load the Virtual CAN kernel module:

```bash
sudo modprobe vcan
```

Create the virtual interface:

```bash
sudo ip link add dev vcan0 type vcan
```

Bring it up:

```bash
sudo ip link set vcan0 up
```

Check it:

```bash
ip -details link show vcan0
```

### If you get `File exists`

That usually means `vcan0` already exists. Do this instead:

```bash
sudo ip link set vcan0 up
```

Then continue.

## 6. Compile the Friend's Version

The easiest method is:

```bash
make clean
make
```

Check the resulting executables:

```bash
ls -lh can_transmitter can_dashboard
```

You should now have two executable files.

## 7. Run the Three-Terminal Test

Do not try to run everything in the same terminal. Use three terminals.

### Terminal 1 — raw CAN monitor

From the project directory:

```bash
candump vcan0
```

This terminal will show the raw frames.

### Terminal 2 — dashboard

Open another terminal and return to the same project directory:

```bash
cd /path/to/AI-Assisted-DBC-SocketCAN-Version-B
./can_dashboard
```

The dashboard will initially wait for frames.

### Terminal 3 — transmitter

Open a third terminal:

```bash
cd /path/to/AI-Assisted-DBC-SocketCAN-Version-B
./can_transmitter
```

Now the transmitter starts generating CAN traffic.

## 8. What Should Happen

### Terminal 3

You should see values similar to:

```text
TX Speed=... km/h | RPM=... | Coolant=... C | Fuel=... % | Battery=... V | Ambient=... C
```

The values move gradually rather than staying constant.

### Terminal 1

You should see repeated frames:

```text
vcan0  100   [8]  ...
vcan0  101   [8]  ...
vcan0  102   [8]  ...
```

### Terminal 2

You should see the decoded values in the dashboard.

The dashboard values should be close to the transmitter values.

## 9. Why There Are Three CAN IDs

The assignment groups the six signals into three messages:

```text
0x100 VehicleStatus
    ├── VehicleSpeed
    └── EngineRPM

0x101 ThermalFuel
    ├── CoolantTemperature
    └── FuelLevel

0x102 PowerStatus
    ├── BatteryVoltage
    └── AmbientTemperature
```

Each message has DLC 8, meaning eight payload bytes.

## 10. Why the DBC Is Necessary

Suppose `candump` shows:

```text
vcan0  100   [8]  20 1C 80 0C 00 00 00 00
```

Those bytes do not automatically mean `72 km/h` or `3200 rpm`.

The DBC tells you:

```text
bits 0-15  -> VehicleSpeed
bits 16-31 -> EngineRPM
```

Then the scaling formula tells you how to convert raw values to physical values.

That is the core concept being demonstrated by this assignment.

## 11. Simple Manual Example

For speed:

```text
Factor = 0.01
Offset = 0
```

So if the raw value is `7250`:

```text
Physical = 7250 × 0.01
         = 72.50 km/h
```

For coolant:

```text
Factor = 0.1
Offset = -40
```

If the raw value is `1200`:

```text
Physical = 1200 × 0.1 - 40
         = 80 degC
```

## 12. Check Individual CAN IDs

Use these commands in another terminal while the transmitter is running.

For `VehicleStatus`:

```bash
candump vcan0,100:7FF
```

For `ThermalFuel`:

```bash
candump vcan0,101:7FF
```

For `PowerStatus`:

```bash
candump vcan0,102:7FF
```

## 13. SavvyCAN

After the basic command-line test works, open SavvyCAN.

Connect it to the SocketCAN `vcan0` interface, then load:

```text
vehicle.dbc
```

Look for the six named signals and plot them over time.

You are using SavvyCAN as an independent check. The project should already work with `candump` and the C dashboard before moving to the GUI.

## 14. What Screenshots to Take

A good assignment evidence set is:

### Screenshot 1

The `vcan0` interface:

```bash
ip -details link show vcan0
```

### Screenshot 2

Successful compilation:

```bash
make clean
make
```

### Screenshot 3

Raw CAN traffic:

```bash
candump vcan0
```

### Screenshot 4

The transmitter running.

### Screenshot 5

The dashboard running.

### Screenshot 6

SavvyCAN displaying the decoded signals, if SavvyCAN is required by your instructor.

## 15. Stop Everything

In the transmitter and dashboard terminals:

```text
Ctrl+C
```

Optionally remove `vcan0`:

```bash
sudo ip link delete vcan0
```

## 16. Put the Friend's Version on GitHub

The easiest method is to create an empty GitHub repository first.

Example repository name:

```text
AI-Assisted-DBC-SocketCAN-Version-B
```

Do not copy the files into your own repository if these are meant to be two independent submissions. Use the friend's repository URL.

Inside the friend's project folder:

```bash
git init
git branch -M main
git add .
git status
git commit -m "Add AI-assisted SocketCAN telemetry project"
```

Then connect the friend's repository:

```bash
git remote add origin https://github.com/FRIEND_USERNAME/FRIEND_REPOSITORY.git
```

Verify:

```bash
git remote -v
```

Push:

```bash
git push -u origin main
```

## 17. If the GitHub Repository Already Has Files

If the remote repository already contains a README or another initial commit, do not blindly overwrite it.

First inspect:

```bash
git remote -v
git fetch origin
git branch -a
```

Then decide whether the repository should be merged or recreated as an empty repository.

For a college assignment, an empty repository before the first push is usually the least confusing setup.

## 18. What Your Final GitHub Repository Should Contain

At minimum:

```text
vehicle.dbc
can_transmitter.c
can_dashboard.c
AI_USAGE_REPORT.md
README.md
```

This Version B repository also includes:

```text
START_HERE.md
Makefile
.gitignore
```

## 19. What Not To Do

Do not:

- change the CAN IDs randomly;
- change factor/offset values without updating both DBC and C;
- copy your other student's source and only rename variables;
- upload compiled binaries as the main source submission;
- claim a physical CAN bus was tested if only `vcan0` was tested;
- rely on SavvyCAN alone without checking raw traffic;
- assume a successful `send()` means the decoded engineering values are correct.

## 20. The Full Process in One Sequence

```text
1. Extract project
2. Enter project folder
3. Install GCC / can-utils / iproute2 / Git
4. Create vcan0
5. Check vcan0
6. Run make
7. Start candump
8. Start dashboard
9. Start transmitter
10. Verify IDs 100/101/102
11. Compare transmitter and dashboard values
12. Load vehicle.dbc into SavvyCAN
13. Take evidence screenshots
14. Initialize Git
15. Commit files
16. Add the FRIEND'S GitHub remote
17. Push main
18. Open GitHub and verify every file
```

That is the assignment workflow from start to finish.

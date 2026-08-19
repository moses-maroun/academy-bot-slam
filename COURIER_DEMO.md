# AcadBot Courier — Demo Runbook

A step-by-step script for running and testing the courier final project,
end to end. Rehearse it in full at least once before presenting it.

---

## Terminal layout

Three terminals, each attached to the container (`cd docker &&
./run_ros_container.sh` — attaches to the already-running container if one
exists):

| Terminal | Purpose |
|---|---|
| **1** | Runs the single bringup command. Stays open and mostly untouched for the whole demo — this is the log/RViz source. |
| **2** | Runs every `ros2 service call` / `ros2 action send_goal` — this is the terminal the audience watches. |
| **3** | Free for one-off checks (health check, `ps aux`, troubleshooting) and, in the cancel scenario, is not needed at all since Terminal 2 does the canceling itself. |

RViz opens automatically as part of Terminal 1's launch — keep it visible
alongside Terminal 2 so costmaps, the global plan, and the robot's motion
are visible while Terminal 2's feedback stream prints.

---

## 0. Pre-flight build check

**Terminal 1:**
```bash
cd docker && ./run_ros_container.sh
cd /ros2_ws && colcon build --symlink-install
```
Expect: `9 packages finished`, no warnings, no errors. Do not proceed to a
live run if this doesn't come back clean.

---

## 1. Bring up the stack

**Terminal 1:**
```bash
ros2 launch acadbot_bringup courier_demo.launch.py
```
One command brings up Gazebo, AMCL, Nav2, RViz, `courier_dispatcher`, and
`courier_executor` together.

Expect a Gazebo window with the robot in the mapped building, an RViz
window with the map loaded, and startup logs ending with both courier
nodes' location tables:
```
[courier_dispatcher]: courier_dispatcher started. 3 known location(s):
[courier_executor]: courier_executor started. 3 known location(s):
```
Repeating `AMCL cannot publish a pose... Please set the initial pose`
warnings are expected at this point — resolved in the next step.

---

## 2. Set the initial pose

In RViz: click **2D Pose Estimate**, then click-and-drag on the map at
roughly the robot's real position in Gazebo, in the direction it's facing.
The particle cloud should collapse onto the robot and the AMCL warnings
should stop. This step is required on every fresh launch.

Robot spawn (Gazebo `(-3.0, -2.0)`) corresponds to map-frame `(0, 0)` — see
the comment at the top of `ros2_ws/src/acadbot_courier/config/locations.yaml`.

---

## 3. Health check

**Terminal 3:**
```bash
ros2 service call /lifecycle_manager_navigation/is_active std_srvs/srv/Trigger
```
Expect `success: true`. If `false`, wait a few seconds and retry — Nav2
needs the pose from Step 2 before its costmaps finish activating. Still
false after ~15s: see Troubleshooting.

Also confirm in RViz that the **Global Costmap** and **Local Costmap**
layers are painting (grey/inflated regions near walls) — on by default,
nothing to enable manually.

---

## 4. The four required scenarios

Any order is acceptable. All commands below go in **Terminal 2** unless
stated otherwise.

### A. Rejected request (unknown location)

**Terminals needed: 2** (Terminal 1 for the running stack/log, Terminal 2
for the command).

```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: nowhere}"
```
Expect, instantly, no robot movement:
```
accepted: false
job_id: ''
reason: "unknown dropoff location 'nowhere'"
```

### B. A delivery that succeeds end to end

**Terminals needed: 2** (Terminal 1, Terminal 2).

**Terminal 2:**
```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: lab_bench}"
```
Expect `accepted: true, job_id: 'job_1'`, instantly, robot still not
moving — the pause demonstrates service and action are separate concerns.

```bash
ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
  "{job_id: job_1}" --feedback
```
Expect feedback at least once a second:
```
leg: to_pickup
heading_to: reception
distance_remaining: ...   # counting down
attempt: 1
```
then, without stopping, the leg switches to `to_dropoff` /
`heading_to: lab_bench`, ending:
```
Result:
    success: true
    final_leg: ''
    message: delivered
Goal finished with status: SUCCEEDED
```
RViz shows a green global plan twice (once per leg) and the robot driving
both legs back to back.

### C. Cancel while driving

**Terminals needed: 2** (Terminal 1, Terminal 2 — no third terminal
needed; the same terminal that sends the goal also sends the cancel).

**Terminal 2:**
```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: lab_bench, dropoff: loading_dock}"
ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
  "{job_id: job_2}" --feedback
```
While it's driving (a couple of feedback lines in), press **Ctrl-C** in
this same terminal.

Expect the robot to visibly slow and stop in RViz within about a second,
and the terminal to show the goal ending canceled, not succeeded.

Confirm the dispatcher is free again:
```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: lab_bench}"
```
Should accept normally.

### D. A blockage — Nav2's recoveries do their work

**Terminals needed: 2**, plus the Gazebo GUI (already open from Terminal
1's launch — no extra terminal required to drop an obstacle).

**Terminal 2:**
```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: lab_bench}"
ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
  "{job_id: job_3}" --feedback
```
While the robot is en route, insert a simple shape from Gazebo's toolbar
directly in its path.

Expect the robot to slow/stop near the obstacle, then recovery behavior (a
spin, a backup, a brief wait) as `behavior_server` clears the way, followed
by a fresh plan (green path changes) attempting to route around it. Two
valid outcomes:
- **Recovers and finishes** — delivery ends `success: true`.
- **Can't get through** — after `max_retries` failed attempts per leg
  (config default: 2 retries, 3 attempts total), the action ends
  `success: false` with `final_leg` naming which leg failed.

---

## Optional: `nav_goal_timeout` in action

What it proves: `nav_goal_timeout` bounds how long a single
`navigate_to_pose` attempt is allowed to run before the courier gives up on
it and counts it as a failed attempt — a backstop against Nav2 getting
stuck without ever returning a result on its own. This is a config knob,
not one of the four required scenarios — treat it as a rehearsed talking
point, not something to attempt live for the first time in front of the
room.

It can't just be started as a second node alongside the running one: the
courier launched by `courier_demo.launch.py` already owns the
`deliver_package` action name, and a second `courier_executor` instance
would collide with it (both would try to answer the same requests, with no
guarantee which one responds — confusing, not a real demo). Stop the
running one first, run the override standalone, then restart the normal
one to leave the stack exactly as it was.

**Terminals needed: 3** (Terminal 1 keeps running the stack untouched;
Terminal 2 sends the delivery; Terminal 3 manages the executor process
itself).

**Terminal 3 — stop the normal executor:**
```bash
ps aux | grep courier_executor | grep -v grep
```
Note the PID of the line ending in `.../courier_executor --ros-args -r
__node:=courier_executor ...` (there's exactly one — `courier.launch.py`
starts nodes directly, no wrapper process to worry about). Then:
```bash
kill -9 <that PID>
```
**Verify it's actually gone before continuing:**
```bash
ps aux | grep courier_executor | grep -v grep
```
This must print nothing. If it still prints a line, the PID was wrong —
recheck it and kill again. Do not proceed to the next step until this is
empty; if the old instance is still alive when the override starts, the
old one (with the real 90s timeout) may be the one that ends up answering
requests, and the override's 3s timeout will simply never appear to fire.

**Terminal 3 — start the override:**
```bash
LOC=/ros2_ws/install/acadbot_courier/share/acadbot_courier/config/locations.yaml
PARAMS=/ros2_ws/install/acadbot_courier/share/acadbot_courier/config/courier_params.yaml
ros2 run acadbot_courier courier_executor --ros-args \
  --params-file $LOC --params-file $PARAMS \
  -p nav_goal_timeout:=3.0 -p max_retries:=0
```
Both the normal param files are still loaded — only `nav_goal_timeout` and
`max_retries` are overridden on top (`max_retries:=0` makes it fail after
exactly one timed-out attempt instead of three, so the demo doesn't repeat
the same drive-and-stop three times).

**Verify the override is the live instance, from a fourth terminal (or
Terminal 2, before sending the delivery):**
```bash
ros2 param get /courier_executor nav_goal_timeout
```
Must print `3.0`. If it prints `90.0`, the step above didn't actually
replace the running node — go back and confirm the kill.

**Also check the simulation isn't running in slow motion.** The timeout is
measured in *simulation* time (`use_sim_time: true`), not your wall clock —
Gazebo running below real-time will make 3 simulated seconds take longer
than 3 real seconds to pass:
```bash
ros2 topic hz /clock
```
Gazebo's own GUI also shows a real-time factor in its bottom status bar. If
it's well under `1.0`, either wait proportionally longer (`3.0 / RTF` real
seconds) or lower `nav_goal_timeout` further for a snappier demo.

**Terminal 2 — send a delivery with real driving distance** (anything a
few meters away — at the robot's ~0.26 m/s top speed, any real leg here
takes well over 3s):
```bash
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: lab_bench}"
ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
  "{job_id: job_4}" --feedback
```

Expect: feedback prints normally for about 3 seconds, then Terminal 3
(the executor's own log) prints
```
[to_pickup] attempt 1 exceeded nav_goal_timeout (3.0s) — canceling
```
the robot stops in RViz on its own — nothing blocked it, the timeout alone
ended the attempt — and Terminal 2's action result comes back:
```
Result:
    success: false
    final_leg: to_pickup
    message: failed to reach reception after 1 attempt(s)
Goal finished with status: ABORTED
```

**Terminal 3 — restore normal state afterward:**
```bash
# Ctrl-C the override instance, then:
ros2 run acadbot_courier courier_executor --ros-args --params-file $LOC --params-file $PARAMS
```
This restarts it with the real `courier_params.yaml` values (no override),
so the rest of the demo continues as if this detour never happened.

---

## Quick reference

```bash
# bring up everything (Terminal 1)
ros2 launch acadbot_bringup courier_demo.launch.py

# health check (Terminal 3)
ros2 service call /lifecycle_manager_navigation/is_active std_srvs/srv/Trigger

# reject (Terminal 2)
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: nowhere}"

# accept + run (Terminal 2)
ros2 service call /request_delivery acadbot_courier_msgs/srv/RequestDelivery \
  "{pickup: reception, dropoff: lab_bench}"
ros2 action send_goal /deliver_package acadbot_courier_msgs/action/DeliverPackage \
  "{job_id: job_1}" --feedback
```
Known locations (from `config/locations.yaml`): `reception`, `lab_bench`,
`loading_dock`.

---

## Troubleshooting

**AMCL warnings never stop.** The initial pose wasn't set, or was set far
from the robot's real position — repeat Step 2.

**`is_active` stays `false` / RViz shows no costmap.** Nav2 tried to
activate before AMCL had a pose. Check Terminal 1 for `Failed to activate
global_costmap` — if present, the bringup gave up; Ctrl-C Terminal 1 and
restart from Step 1, setting the pose faster. On a slow machine, relaunch
with `nav2_delay:=20`.

**A request is rejected as "robot busy" with no job in flight.** An earlier
job was accepted but its action goal was never sent. It self-frees after
`pending_job_timeout` (30s) — wait, or restart the courier nodes.

**Something looks stuck or duplicated.** `ps aux | grep courier` should
show exactly one `courier_dispatcher` and one `courier_executor` process.
Extras: kill by exact PID (`kill -9 <pid>`), not by name pattern — a broad
`pkill -f courier` can match the invoking shell's own command line and kill
more than intended.

**Need a clean restart.** Ctrl-C Terminal 1 once — `ros2 launch` tears down
its whole process tree correctly. Return to Step 1.

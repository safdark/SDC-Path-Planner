#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "3rdparty/Eigen-3.3/Eigen/Core"
#include "3rdparty/Eigen-3.3/Eigen/QR"
#include "3rdparty/json.hpp"
#include "3rdparty/spline.h"
#include "utils/helpers.h"

//#include "core/worldmap.h"
//#include "core/autonomous.h"
//#include "core/behaviorplanner.h"
//#include "core/pathplanner.h"
//#include "core/sensorfusion.h"
//#include "core/localization.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;
using std::shared_ptr;
using std::getline;

// Sensor Fusion Indices:
#define SENSOR_VX 3
#define SENSOR_VY 4
#define SENSOR_S 5
#define SENSOR_D 6

#define TIME_INCREMENT 0.02
#define PATH_POINTS 50
#define REFERENCE_VELOCITY 49.5
#define SPEED_INCREMENT 0.224
#define SPEED_DECREMENT 0.224

void readWaypoints(vector<double> &map_waypoints_x, vector<double> &map_waypoints_y, vector<double> &map_waypoints_s,
                   vector<double> &map_waypoints_dx, vector<double> &map_waypoints_dy, const std::ifstream &in_map_);

int main() {
    uWS::Hub h;

    // Load up map values for waypoint's x,y,s and d normalized normal vectors
    vector<double> map_waypoints_x;
    vector<double> map_waypoints_y;
    vector<double> map_waypoints_s;
    vector<double> map_waypoints_dx;
    vector<double> map_waypoints_dy;

    // Waypoint map to read from
    string map_file_ = "../data/highway_map.csv";
    std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

    // The max s value before wrapping around the track back to 0
    double max_s = 6945.554;

    string line;
    while (getline(in_map_, line)) {
        std::istringstream iss(line);
        double x;
        double y;
        float s;
        float d_x;
        float d_y;
        iss >> x;
        iss >> y;
        iss >> s;
        iss >> d_x;
        iss >> d_y;
        map_waypoints_x.push_back(x);
        map_waypoints_y.push_back(y);
        map_waypoints_s.push_back(s);
        map_waypoints_dx.push_back(d_x);
        map_waypoints_dy.push_back(d_y);
    }




    // Initialize the autonomous vehicle:
    int lane = 1;   // Car starts out in this lane
    double ref_vel = (double) REFERENCE_VELOCITY;

//    WorldMap_ world(new WorldMap(map_waypoints_x, map_waypoints_y, map_waypoints_s, map_waypoints_dx, map_waypoints_dy, max_s));
//    PathPlanner_ pathPlanner (new StationaryPathPlanner());
//    BehaviorPlanner_ behaviorPlanner (new NoopBehaviorPlanner());
//    AutonomousVehicle_ ego (new AutonomousVehicle(world, behaviorPlanner, pathPlanner));






    h.onMessage([&ref_vel, &lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
        // "42" at the start of the message means there's a websocket message event.
        // The 4 signifies a websocket message
        // The 2 signifies a websocket event
        if (length && length > 2 && data[0] == '4' && data[1] == '2') {

            auto s = hasData(data);

            if (s != "") {
                auto j = json::parse(s);

                string event = j[0].get<string>();

                if (event == "telemetry") {
                    // j[1] is the data JSON object

                    // Main car's localization Data
                    double car_x = j[1]["x"];
                    double car_y = j[1]["y"];
                    double car_s = j[1]["s"];
                    double car_d = j[1]["d"];
                    double car_yaw = j[1]["yaw"];
                    double car_speed = j[1]["speed"];

                    // Previous path data given to the Planner
                    auto previous_path_x = j[1]["previous_path_x"];
                    auto previous_path_y = j[1]["previous_path_y"];

                    // Previous path's end s and d values
                    double end_path_s = j[1]["end_path_s"];
                    double end_path_d = j[1]["end_path_d"];

                    // Sensor Fusion Data, a list of all other cars on the same side of the road.
                    auto sensor_fusion = j[1]["sensor_fusion"];

                    /* ============================================================== */
                    /**
                    * TODO: define a path made up of (x,y) points that the car will visit
                    *   sequentially every .02 seconds
                    */

                    int previous_size = previous_path_x.size();
                    if (previous_size > 0) {
                        car_s = end_path_s;
                    }

                    bool too_close = false;

                    // find ref_v to use:

                    for (int i = 0; i<sensor_fusion.size(); i++) {
                        float d = sensor_fusion[i][SENSOR_D];

                        // other car is in my lane
                        if ((d < (2 + (4*lane) + 2) && (d > 2+ (4 * lane) - 2))) {
                            double vx = sensor_fusion[i][SENSOR_VX];
                            double vy = sensor_fusion[i][SENSOR_VY];
                            double check_speed = sqrt(vx*vx + vy*vy);
                            double check_car_s = sensor_fusion[i][SENSOR_S];

                            check_car_s+=((double) previous_size * TIME_INCREMENT * check_speed); // If using previous points can project s value out

                            // Check s values greater than mine and s gap:
                            if ((check_car_s > car_s) && ((check_car_s < 30))) {

                                  // Do some logic here: Lower reference velocity so we don't crash into the car in front of us,
                                  // could also flag to try to change lanes
                                  ref_vel = 29.5;

                            }

                        }
                    }

                    // Reduce or increase the speed gradually
                    if (too_close) {
                        ref_vel -= SPEED_INCREMENT;
                    } else if (ref_vel < 49.5) {
                        ref_vel += SPEED_DECREMENT;
                    }


                    // Create a list of widely spaced (x,y) waypoints, evenly spaced at 30m
                    // Later we will interpolate these waypoints with a spline and fill it in with more points to control speed
                    vector<double> ptsx;
                    vector<double> ptsy;

                    // Reference x, y and yaw states
                    // Either we will reference the starting point as where the car is, or at the previous path's end point
                    double ref_x = car_x;
                    double ref_y = car_y;
                    double ref_yaw = deg2rad(car_yaw);

                    // If previous size is almost empty, use the car as starting reference:
                    if (previous_size < 2) {
                        // use 2 points that make the path tangent to the car
                        double prev_car_x = car_x - cos(car_yaw);
                        double prev_car_y = car_y - sin(car_yaw);

                        ptsx.push_back(prev_car_x);
                        ptsx.push_back(car_x);

                        ptsy.push_back(prev_car_y);
                        ptsy.push_back(car_y);
                    } else {
                        // use the previous path's end point as starting reference
                        ref_x = previous_path_x[previous_size-1];
                        ref_y = previous_path_y[previous_size-1];

                        double ref_x_prev = previous_path_x[previous_size-2];
                        double ref_y_prev = previous_path_y[previous_size-2];
                        ref_yaw = atan2((ref_y-ref_y_prev), (ref_x-ref_x_prev));

                        // use two points that make the path tanagent to the two previous path's end points:
                        ptsx.push_back(ref_x_prev);
                        ptsx.push_back(ref_x);

                        ptsy.push_back(ref_y_prev);
                        ptsy.push_back(ref_y);

                    }


                    // In frenet, add evenly 30m spaced poitns ahead of the starting reference:
                    vector<double> next_wp0 = getXY(car_s + 30, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
                    vector<double> next_wp1 = getXY(car_s + 60, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
                    vector<double> next_wp2 = getXY(car_s + 90, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

                    ptsx.push_back(next_wp0[0]);
                    ptsx.push_back(next_wp1[0]);
                    ptsx.push_back(next_wp2[0]);

                    ptsy.push_back(next_wp0[1]);
                    ptsy.push_back(next_wp1[1]);
                    ptsy.push_back(next_wp2[1]);

                    // Shift frame to 0 degrees for all the widely spaced points:
                    for (int i = 0; i < ptsx.size() ; i++) {
                        double shift_x = ptsx[i] - ref_x;
                        double shift_y = ptsy[i] - ref_y;

                        ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
                        ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));
                    }


                    tk::spline spline;
                    spline.set_points(ptsx, ptsy);

                    // Actual (x,y) points to be driven (returned to the simulator)
                    vector<double> next_x_vals;
                    vector<double> next_y_vals;


                    // First, copy over all the previous path points from the last time
                    for (int i = 0; i<previous_path_x.size(); i++) {
                        next_x_vals.push_back(previous_path_x[i]);
                        next_y_vals.push_back(previous_path_y[i]);
                    }

                    double target_x = 30.0;
                    double target_y = spline(target_x);
                    double target_dist = sqrt((target_x*target_x) + (target_y*target_y));

                    double x_add_on = 0;
                    // Fill up the REMAINING planned path after filling it with previous points, here we will always output 50 points:
                    for(int i = 1; i<= (PATH_POINTS - previous_path_x.size()); i++) {
                        double N = (target_dist / (TIME_INCREMENT * ref_vel/2.24));
                        double x_point = x_add_on+(target_x)/N;
                        double y_point = spline(x_point);

                        x_add_on = x_point;

                        double x_ref = x_point;
                        double y_ref = y_point;

                        // Rotate back to normal after rotating earlier:
                        x_point = (x_ref * cos(ref_yaw)-y_ref*sin(ref_yaw));
                        y_point = (x_ref * sin(ref_yaw)+y_ref*cos(ref_yaw));

                        x_point += ref_x;
                        y_point += ref_y;

                        next_x_vals.push_back(x_point);
                        next_y_vals.push_back(y_point);
                    }





                    /* ============================================================== */

                      json msgJson;
                      msgJson["next_x"] = next_x_vals;
                      msgJson["next_y"] = next_y_vals;

                      auto msg = "42[\"control\","+ msgJson.dump()+"]";

                      ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
                    }  // end "telemetry" if
                  } else {
                    // Manual driving
                    std::string msg = "42[\"manual\",{}]";
                    ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
                  }
        }  // end websocket if
    }); // end h.onMessage

    h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
    });

    h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
    });

    int port = 4567;
    if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
    } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
    }

    h.run();
}

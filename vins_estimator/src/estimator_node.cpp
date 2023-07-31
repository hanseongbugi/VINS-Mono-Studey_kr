#include <stdio.h>
#include <queue>
#include <map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>

#include "estimator.h"
#include "parameters.h"
#include "utility/visualization.h"


Estimator estimator;

std::condition_variable con;
double current_time = -1;
queue<sensor_msgs::ImuConstPtr> imu_buf;
queue<sensor_msgs::PointCloudConstPtr> feature_buf;
queue<sensor_msgs::PointCloudConstPtr> relo_buf;
int sum_of_wait = 0;

std::mutex m_buf;
std::mutex m_state;
std::mutex i_buf;
std::mutex m_estimator;

double latest_time;
Eigen::Vector3d tmp_P;
Eigen::Quaterniond tmp_Q;
Eigen::Vector3d tmp_V;
Eigen::Vector3d tmp_Ba;
Eigen::Vector3d tmp_Bg;
Eigen::Vector3d acc_0;
Eigen::Vector3d gyr_0;
bool init_feature = 0;
bool init_imu = 1;
double last_imu_t = 0;

void predict(const sensor_msgs::ImuConstPtr &imu_msg)
{
    double t = imu_msg->header.stamp.toSec();
    if (init_imu) //�ʱ�ȭ �����̸�
    {
        latest_time = t; //������ �ð��� ���� imu timestamp�� �ʱ�ȭ
        init_imu = 0; //���̻� �ʱ�ȭ ���� �ʵ��� �÷��׸� 0���� ����
        return; //�Լ� ����
    }
    double dt = t - latest_time; //timestamp�� ������ ����
    latest_time = t; //������ �ð��� ���� imu timestamp�� �ʱ�ȭ

    double dx = imu_msg->linear_acceleration.x; //timestamp�� �ش��ϴ� ���ӵ� ���� ���� �����´�.
    double dy = imu_msg->linear_acceleration.y;
    double dz = imu_msg->linear_acceleration.z;
    Eigen::Vector3d linear_acceleration{dx, dy, dz}; //������ ������ ���� 3x1 ��ķ� �����.

    double rx = imu_msg->angular_velocity.x; //timestamp�� �ش��ϴ� ���̷� ���� ���� �����´�.
    double ry = imu_msg->angular_velocity.y;
    double rz = imu_msg->angular_velocity.z;
    Eigen::Vector3d angular_velocity{rx, ry, rz}; //������ ������ ���� 3x1 ��ķ� �����.

    Eigen::Vector3d un_acc_0 = tmp_Q * (acc_0 - tmp_Ba) - estimator.g; // ������ ������ ���ӵ� �� acc_0���� Bias�� ������ ��

    Eigen::Vector3d un_gyr = 0.5 * (gyr_0 + angular_velocity) - tmp_Bg; //  IMU���� ������ ���̷� �� gyr_0�� ���ӵ��� ����� ���ϰ� Bias��ŭ ����
    tmp_Q = tmp_Q * Utility::deltaQ(un_gyr * dt); // ���̷� ������ imu ������ ������ ��Ÿ���� ȸ�� ����� ���Ͽ� �� ����

    Eigen::Vector3d un_acc_1 = tmp_Q * (linear_acceleration - tmp_Ba) - estimator.g; //callback���� ���� ���ӵ� ���� ���� ����

    Eigen::Vector3d un_acc = 0.5 * (un_acc_0 + un_acc_1); //�� ������ ����� ���Ѵ�.

    tmp_P = tmp_P + dt * tmp_V + 0.5 * dt * dt * un_acc; // ��� ���� ���� ��ġ ����
    tmp_V = tmp_V + dt * un_acc;  // ��� ���� ���� �ӵ� ����

    acc_0 = linear_acceleration; //���� ���ӵ��� ���̷� ���� ���� ������ ����
    gyr_0 = angular_velocity;
}

void update()
{
    TicToc t_predict;
    latest_time = current_time;
    tmp_P = estimator.Ps[WINDOW_SIZE]; //��ġ
    tmp_Q = estimator.Rs[WINDOW_SIZE]; // IMU ������ ��ġ �� ������ ��Ÿ���� ȸ�� ���
    tmp_V = estimator.Vs[WINDOW_SIZE]; //�ӵ� 
    tmp_Ba = estimator.Bas[WINDOW_SIZE]; //���ӵ� bias ��
    tmp_Bg = estimator.Bgs[WINDOW_SIZE]; //���̷� bias ��
    acc_0 = estimator.acc_0; // ���ӵ� ����
    gyr_0 = estimator.gyr_0; // ���̷� ����

    queue<sensor_msgs::ImuConstPtr> tmp_imu_buf = imu_buf; // imu ���� ����� ���۸� ����
    for (sensor_msgs::ImuConstPtr tmp_imu_msg; !tmp_imu_buf.empty(); tmp_imu_buf.pop()) // ���� �� ��ȸ
        predict(tmp_imu_buf.front()); // ���ۿ� ���� ���� ���� ���� �ӵ� �� ��ġ ����

}

std::vector<std::pair<std::vector<sensor_msgs::ImuConstPtr>, sensor_msgs::PointCloudConstPtr>>
getMeasurements()
{
    std::vector<std::pair<std::vector<sensor_msgs::ImuConstPtr>, sensor_msgs::PointCloudConstPtr>> measurements; // ���� ���� ���� ��� �迭

    while (true)
    {
        if (imu_buf.empty() || feature_buf.empty()) // imu ���ۿ� feature ���۰� ��� ������ 
            return measurements; // �迭�� ��ȯ�ϰ� ����

        // ���� �ֱٿ� ���� ���� ���� time stamp ���� ������ ���� �տ� �ִ� �̹����� time stamp + imu �ð��� �̹��� �ð����� ���� ������ �۰ų� ������
        //  => �ֱٿ� ������ ���� �����Ͱ� ������ ù �̹������� ������ �����Ǿ��ٸ�
        if (!(imu_buf.back()->header.stamp.toSec() > feature_buf.front()->header.stamp.toSec() + estimator.td))
        {
            //ROS_WARN("wait for imu, only should happen at the beginning");
            sum_of_wait++; // ���� ������ ��� Ƚ�� ����
            return measurements; // �迭�� ��ȯ�ϰ� ����
        }
        // ���� ���� ���� ���� ���� time stamp ���� ������ ���� �տ� �ִ� �̹����� time stamp + imu �ð��� �̹��� �ð����� ���� ������ ũ�ų� ������
        //  => ���� ���� ������ ���� �����Ͱ� ������ ù �̹��� ���Ŀ� �����Ǿ��ٸ�
        if (!(imu_buf.front()->header.stamp.toSec() < feature_buf.front()->header.stamp.toSec() + estimator.td))
        {
            ROS_WARN("throw img, only should happen at the beginning");
            feature_buf.pop(); //feature ������ �տ� �ִ� �̹��� ������ ������.
            continue; // ���� loop�� �̵�
        }
        sensor_msgs::PointCloudConstPtr img_msg = feature_buf.front(); // feature ������ ���� �տ� �ִ� �̹��� ������ ������
        feature_buf.pop(); // ���� �տ� �ִ� �̹��� ������ ���ۿ��� ������.

        std::vector<sensor_msgs::ImuConstPtr> IMUs; // imu ������ ���� �迭
        while (imu_buf.front()->header.stamp.toSec() < img_msg->header.stamp.toSec() + estimator.td) // ������ time stamp�� �̹����� time stamp���� ������ �ݺ�
        {
            IMUs.emplace_back(imu_buf.front()); // �迭�� ���� �����͸� �ְ�
            imu_buf.pop(); //���ۿ��� ���� ����
        }
        IMUs.emplace_back(imu_buf.front()); // �ݺ� ���ǿ��� ���� ���� ����� imu �����͵� �迭�� ����
        if (IMUs.empty()) // �迭�� ��� �ִٸ�
            ROS_WARN("no imu between two image");
        measurements.emplace_back(IMUs, img_msg); // ���� ���� ���� ��� �迭�� imu �迭�� image ������ �ִ´�.
    }
    return measurements; // ���� �迭 ��ȯ
}

void imu_callback(const sensor_msgs::ImuConstPtr &imu_msg)
{
    if (imu_msg->header.stamp.toSec() <= last_imu_t) //���� ���� imu�� timestamp�� ������ timestamp���� �۰ų� ������
    {
        ROS_WARN("imu message in disorder!"); //��� �޽��� ��� �� �Լ� ����
        return;
    }
    last_imu_t = imu_msg->header.stamp.toSec(); //������ imu�� timestamp�� ���� ���� timestamp�� �ʱ�ȭ

    m_buf.lock(); //lock
    imu_buf.push(imu_msg); //���ۿ� �޽���(imu ��)�� ����
    m_buf.unlock(); //unlock
    con.notify_one(); //��� ���� ������ �ϳ��� �����.

    last_imu_t = imu_msg->header.stamp.toSec(); //������ imu�� timestamp�� ���� ���� timestamp�� �ʱ�ȭ

    {
        std::lock_guard<std::mutex> lg(m_state); //��� ���� lock�� �Ǵ�.
        predict(imu_msg); // ���� ���� ���� ���� ���� �ӵ� �� ��ġ ����
        std_msgs::Header header = imu_msg->header; // ���� ���� ���� ���� ��� ��(time stamp ���� ����)�� ����
        header.frame_id = "world"; // ���� �����Ϳ��� frame ������ �����Ƿ� ���Ƿ� world�� ��
        if (estimator.solver_flag == Estimator::SolverFlag::NON_LINEAR) // ���� ���� NON_LINEAR �����̸� (INITIAL ��NON_LINEAR 2���� �� ������)
            pubLatestOdometry(tmp_P, tmp_Q, tmp_V, header); //���� ������ �ٸ� ���� ������.
    }
}


void feature_callback(const sensor_msgs::PointCloudConstPtr &feature_msg)
{
    if (!init_feature) // ó�� �̹��� �����Ͱ� ������ ����
    {
        //skip the first detected feature, which doesn't contain optical flow speed
        init_feature = 1;
        return;
    }
    m_buf.lock(); // lock
    feature_buf.push(feature_msg);
    m_buf.unlock(); // unlock
    con.notify_one(); // sleep�ϰ� �ִ� ������ �� �ϳ��� �����.
}

void restart_callback(const std_msgs::BoolConstPtr &restart_msg)
{
    if (restart_msg->data == true) // �ٽ� �����϶�� message ���� ��
    {
        ROS_WARN("restart the estimator!"); // ��� �� �ʱ�ȭ
        m_buf.lock();
        while(!feature_buf.empty())
            feature_buf.pop();
        while(!imu_buf.empty())
            imu_buf.pop();
        m_buf.unlock();
        m_estimator.lock();
        estimator.clearState();
        estimator.setParameter();
        m_estimator.unlock();
        current_time = -1;
        last_imu_t = 0;
    }
    return;
}

void relocalization_callback(const sensor_msgs::PointCloudConstPtr &points_msg)
{
    //printf("relocalization callback! \n");
    m_buf.lock();
    relo_buf.push(points_msg); // relocalization ���ۿ� massage ����
    m_buf.unlock();
}

// thread: visual-inertial odometry
// VIO�� �����ϴ� ������
void process()
{
    while (true)
    {
        std::vector<std::pair<std::vector<sensor_msgs::ImuConstPtr>, sensor_msgs::PointCloudConstPtr>> measurements; // ���� ���� ���� ��� �迭 ����
        std::unique_lock<std::mutex> lk(m_buf); // �� ���� ����
        con.wait(lk, [&]
                 {
            return (measurements = getMeasurements()).size() != 0;
                 }); // ���� �����Ͱ� ���� �� ���� ���
        lk.unlock(); // unlock
        m_estimator.lock(); // lock
        for (auto &measurement : measurements) // ���� ���� �� �迭�� ��Ҹ� ��ȸ
        {
            auto img_msg = measurement.second; // image ������ �����´�.
            double dx = 0, dy = 0, dz = 0, rx = 0, ry = 0, rz = 0;
            for (auto &imu_msg : measurement.first) // �ش� image ������ ������ imu �����͸� ��ȸ
            {
                double t = imu_msg->header.stamp.toSec(); // imu �������� time stamp�� �����´�.
                double img_t = img_msg->header.stamp.toSec() + estimator.td; // �̹��� time stamp�� imu���� �ð� ���̸� ���ؼ� �̹����� time stamp�� ��
                if (t <= img_t) // imu�� time stamp�� �̹����� time stamp���� �۰ų� ������
                { 
                    if (current_time < 0) // ���� �ð��� �ʱ�ȭ���� �ʾ�����
                        current_time = t; // ���� �ð��� imu�� time stamp�� �ʱ�ȭ
                    double dt = t - current_time; // dt�� imu�� time stamp�� ���� �ð��� ���� ������ ��
                    ROS_ASSERT(dt >= 0); // dt�� 0���� ���� ��� ���α׷��� ���߰� ���� �޽����� ���
                    current_time = t; // ���� �ð��� imu�� time stamp�� �ʱ�ȭ
                    dx = imu_msg->linear_acceleration.x; // ���ӵ� ������ x, y, z ���� ������
                    dy = imu_msg->linear_acceleration.y;
                    dz = imu_msg->linear_acceleration.z;
                    rx = imu_msg->angular_velocity.x; // ���̷� ������ x, y, z ���� ������
                    ry = imu_msg->angular_velocity.y;
                    rz = imu_msg->angular_velocity.z;
                    estimator.processIMU(dt, Vector3d(dx, dy, dz), Vector3d(rx, ry, rz)); // dt�� ���ӵ� ���� ��, ���̷� ���� ���� �ְ� �ӵ�, ��ġ, ȸ�� ����
                    //printf("imu: dt:%f a: %f %f %f w: %f %f %f\n",dt, dx, dy, dz, rx, ry, rz);

                }
                else // ū ���
                {
                    double dt_1 = img_t - current_time; // �̹����� time stamp�� ���� �ð��� ���̸� dt_1��
                    double dt_2 = t - img_t; // imu time stamp�� image time stamp�� ���̸� dt_2��
                    current_time = img_t; // ���� �ð� ����
                    ROS_ASSERT(dt_1 >= 0); // dt_1�� 0���� ���� ��� ����
                    ROS_ASSERT(dt_2 >= 0); // dt_2�� 0���� ���� ��� ����
                    ROS_ASSERT(dt_1 + dt_2 > 0); // dt_1 + dt_2�� 0 ������ ��� ����
                    double w1 = dt_2 / (dt_1 + dt_2); //dt_1�� dt_2�� ����ġ�� ����
                    double w2 = dt_1 / (dt_1 + dt_2);
                    dx = w1 * dx + w2 * imu_msg->linear_acceleration.x; // ����ġ�� ���� ���� ȥ���Ͽ� ���̷� ���ӵ� ���� ����
                    dy = w1 * dy + w2 * imu_msg->linear_acceleration.y;
                    dz = w1 * dz + w2 * imu_msg->linear_acceleration.z;
                    rx = w1 * rx + w2 * imu_msg->angular_velocity.x;
                    ry = w1 * ry + w2 * imu_msg->angular_velocity.y;
                    rz = w1 * rz + w2 * imu_msg->angular_velocity.z;
                    estimator.processIMU(dt_1, Vector3d(dx, dy, dz), Vector3d(rx, ry, rz)); // dt�� ���ӵ� ���� ��, ���̷� ���� ���� �ְ� �ӵ�, ��ġ, ȸ�� ����
                    //printf("dimu: dt:%f a: %f %f %f w: %f %f %f\n",dt_1, dx, dy, dz, rx, ry, rz);
                }
            }
            // set relocalization frame
            sensor_msgs::PointCloudConstPtr relo_msg = NULL; // relocalization massage ������ ����
            while (!relo_buf.empty()) // ���ۿ� ������ ������
            {
                relo_msg = relo_buf.front(); // ������ �� �κ��� ������.
                relo_buf.pop();
            }
            if (relo_msg != NULL) // massage�� ������ ������
            {
                vector<Vector3d> match_points; // ��Ī���� ���� �迭 ����
                double frame_stamp = relo_msg->header.stamp.toSec(); // relocalization�� frame�� time stamp�� ������
                for (unsigned int i = 0; i < relo_msg->points.size(); i++) // ��Ī ���� ���� ��ŭ ��ȸ
                {
                    Vector3d u_v_id; 
                    u_v_id.x() = relo_msg->points[i].x; // ��Ī���� x, y, z ���� ����
                    u_v_id.y() = relo_msg->points[i].y;
                    u_v_id.z() = relo_msg->points[i].z;
                    match_points.push_back(u_v_id); // �迭�� ����
                }
                Vector3d relo_t(relo_msg->channels[0].values[0], relo_msg->channels[0].values[1], relo_msg->channels[0].values[2]); // keyframe�� ���� ��ȯ ���
                Quaterniond relo_q(relo_msg->channels[0].values[3], relo_msg->channels[0].values[4], relo_msg->channels[0].values[5], relo_msg->channels[0].values[6]); // key frame�� ���� ȸ�� ���
                Matrix3d relo_r = relo_q.toRotationMatrix(); // ���ʹϾ��� Matrix�� ��ȯ
                int frame_index;
                frame_index = relo_msg->channels[0].values[7]; // frame�� index ����
                estimator.setReloFrame(frame_stamp, frame_index, match_points, relo_t, relo_r); // relocalization�� frame ����
            }

            ROS_DEBUG("processing vision data with stamp %f \n", img_msg->header.stamp.toSec());

            TicToc t_s;
            map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> image;
            for (unsigned int i = 0; i < img_msg->points.size(); i++)
            {
                int v = img_msg->channels[0].values[i] + 0.5;
                int feature_id = v / NUM_OF_CAM;
                int camera_id = v % NUM_OF_CAM;
                double x = img_msg->points[i].x;
                double y = img_msg->points[i].y;
                double z = img_msg->points[i].z;
                double p_u = img_msg->channels[1].values[i];
                double p_v = img_msg->channels[2].values[i];
                double velocity_x = img_msg->channels[3].values[i];
                double velocity_y = img_msg->channels[4].values[i];
                ROS_ASSERT(z == 1);
                Eigen::Matrix<double, 7, 1> xyz_uv_velocity;
                xyz_uv_velocity << x, y, z, p_u, p_v, velocity_x, velocity_y;
                image[feature_id].emplace_back(camera_id,  xyz_uv_velocity);
            }
            estimator.processImage(image, img_msg->header);

            double whole_t = t_s.toc();
            printStatistics(estimator, whole_t);
            std_msgs::Header header = img_msg->header;
            header.frame_id = "world";

            pubOdometry(estimator, header);
            pubKeyPoses(estimator, header);
            pubCameraPose(estimator, header);
            pubPointCloud(estimator, header);
            pubTF(estimator, header);
            pubKeyframe(estimator);
            if (relo_msg != NULL)
                pubRelocalization(estimator);
            //ROS_ERROR("end: %f, at %f", img_msg->header.stamp.toSec(), ros::Time::now().toSec());
        }
        m_estimator.unlock();
        m_buf.lock();
        m_state.lock();
        if (estimator.solver_flag == Estimator::SolverFlag::NON_LINEAR)
            update();
        m_state.unlock();
        m_buf.unlock();
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "vins_estimator"); //ROS node�� ��� (�����带 ����ٰ� ����)
    ros::NodeHandle n("~"); //ROS �Ű����� ������ ���� namesapce�κ��� �Ű� ������ ������ (config���� ����, vins_folder ����)
    ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Info); //ROS �α� ���� ���� (���α׷��� ���� �߿� �߻��ϴ� �̺�Ʈ�� ���� ������ ���, info�� �Ϲ����� ���� ����)
    readParameters(n); //�Ű������� �д´�. (paramerters�� readParameters �Լ� ȣ��)
    estimator.setParameter(); //�о�� �Ű������� estimator ��ü�� ����
#ifdef EIGEN_DONT_PARALLELIZE
    ROS_DEBUG("EIGEN_DONT_PARALLELIZE");
#endif
    ROS_WARN("waiting for image and imu..."); //�̹����� imu �����͸� ��ٸ���.

    registerPub(n); //�޽������� publish�ϴ� �Լ�(utility/visualization.cpp)

    // /imu0 ��� ������ �����ϰ�, �޽��� ť ũ��� 2000���� ����,���ο� �޽����� ���� img_callback�Լ��� ȣ��(callback�Լ�), TCP ���� ��� �ּ�ȭ
    ros::Subscriber sub_imu = n.subscribe(IMU_TOPIC, 2000, imu_callback, ros::TransportHints().tcpNoDelay());
    // /feature_tracker/feature ��� ������ �����ϰ�, �޽��� ť ũ��� 2000���� ����,���ο� �޽����� ���� feature_callback�Լ��� ȣ��(callback�Լ�)
    ros::Subscriber sub_image = n.subscribe("/feature_tracker/feature", 2000, feature_callback);
    // /feature_tracker/restart ��� ������ �����ϰ�, �޽��� ť ũ��� 2000���� ����,���ο� �޽����� ���� restart_callback�Լ��� ȣ��(callback�Լ�)
    ros::Subscriber sub_restart = n.subscribe("/feature_tracker/restart", 2000, restart_callback);
    // /pose_graph/match_points ��� ������ �����ϰ�, �޽��� ť ũ��� 2000���� ����,���ο� �޽����� ���� relocalization_callback�Լ��� ȣ��(callback�Լ�)
    ros::Subscriber sub_relo_points = n.subscribe("/pose_graph/match_points", 2000, relocalization_callback);

    std::thread measurement_process{process}; //process �Լ��� ȣ���ϴ� thread ���� �� ����
    ros::spin(); //���α׷��� ����� �� ���� �ݹ� �Լ� ó���� ��ٸ�

    return 0;
}

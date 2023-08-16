#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/PointCloud.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/Bool.h>
#include <cv_bridge/cv_bridge.h>
#include <message_filters/subscriber.h>

#include "feature_tracker.h"

#define SHOW_UNDISTORTION 0

vector<uchar> r_status;
vector<float> r_err;
queue<sensor_msgs::ImageConstPtr> img_buf;

ros::Publisher pub_img,pub_match;
ros::Publisher pub_restart;

FeatureTracker trackerData[NUM_OF_CAM];
double first_image_time;
int pub_count = 1;
bool first_image_flag = true;
double last_image_time = 0;
bool init_pub = 0;

void img_callback(const sensor_msgs::ImageConstPtr &img_msg)
{
    if(first_image_flag) // ù frame �÷��װ� true�� ���
    { 
        first_image_flag = false; // ù frame�� ���Ա⿡ false�� ����
        first_image_time = img_msg->header.stamp.toSec(); // ù frame�� time stamp�� ����
        last_image_time = img_msg->header.stamp.toSec(); // ���� frame�� time stamp�� �ʱ�ȭ
        return; // �Լ� ����
    }
    // detect unstable camera stream
    // ���� frame�� time stamp�� ���� frame�� time stamp���� 1�� ���Ŀ� ���԰ų� ���� frame�� ���� �����Ӻ��� ������ ���� ���
    if (img_msg->header.stamp.toSec() - last_image_time > 1.0 || img_msg->header.stamp.toSec() < last_image_time)
    {
        ROS_WARN("image discontinue! reset the feature tracker!");
        first_image_flag = true;  // feature tracker �ʱ�ȭ�� ���� flag �ʱ�ȭ
        last_image_time = 0;
        pub_count = 1;
        std_msgs::Bool restart_flag;
        restart_flag.data = true;
        pub_restart.publish(restart_flag); // restart flag�� node�� ���� (estimator � ������)
        return;
    }
    last_image_time = img_msg->header.stamp.toSec(); // ���� frame�� time stamp�� ����
    // frequency control
    // node�� frame�� ���� Ƚ�� / (���� frame�� time stmap - ù frame�� time stamp)�� �ø� ������ ����� tracking �� �� (10HZ) ������ ���
    if (round(1.0 * pub_count / (img_msg->header.stamp.toSec() - first_image_time)) <= FREQ) 
    {
        PUB_THIS_FRAME = true; // ���� frame�� frequency �ȿ� �ֱ⿡ true�� ����
        // reset the frequency control
        // node�� frame�� ���� Ƚ�� / (���� frame�� time stmap - ù frame�� time stamp) - Freqency�� ���� ���� tracking �� �� * 0.01 �̸��� ���
        if (abs(1.0 * pub_count / (img_msg->header.stamp.toSec() - first_image_time) - FREQ) < 0.01 * FREQ)
        {
            first_image_time = img_msg->header.stamp.toSec(); // ù �������� time stamp�� ���� frame�� timestamp�� ����
            pub_count = 0; // node�� frame�� ���̿� Ƚ�� �ʱ�ȭ 
        }
    }
    else
        PUB_THIS_FRAME = false; // publish���� �ʴ� frame���� ����

    cv_bridge::CvImageConstPtr ptr; //ROS���� �̹��� �޽����� OpenCV �̹��� ���� ��ȯ�� �����ϴ� �� ���Ǵ� �����͸� ����
    if (img_msg->encoding == "8UC1") // �޽����� ���ڵ� ����� 8UC1(8��Ʈ unsigned char ������ ��� �̹���)�� ���
    {
        sensor_msgs::Image img;
        img.header = img_msg->header; // �޽����� �ִ� ������ OpenCv�� ������ �� �ִ� ���·� ��ȯ
        img.height = img_msg->height;
        img.width = img_msg->width;
        img.is_bigendian = img_msg->is_bigendian;
        img.step = img_msg->step;
        img.data = img_msg->data;
        img.encoding = "mono8";
        ptr = cv_bridge::toCvCopy(img, sensor_msgs::image_encodings::MONO8); // frame ����
    }
    else
        ptr = cv_bridge::toCvCopy(img_msg, sensor_msgs::image_encodings::MONO8); // frame ����

    cv::Mat show_img = ptr->image; // Mat ��ü�� frame ����
    TicToc t_r;
    for (int i = 0; i < NUM_OF_CAM; i++) // ī�޶� ����ŭ �ݺ�
    {
        ROS_DEBUG("processing camera %d", i);
        if (i != 1 || !STEREO_TRACK) // mono�� ���
            trackerData[i].readImage(ptr->image.rowRange(ROW * i, ROW * (i + 1)), img_msg->header.stamp.toSec()); // FeatureTracker ��ü�� ���·� �迭�� ����
        else // �̰��� ������� ����
        {
            if (EQUALIZE)
            {
                cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
                clahe->apply(ptr->image.rowRange(ROW * i, ROW * (i + 1)), trackerData[i].cur_img);
            }
            else
                trackerData[i].cur_img = ptr->image.rowRange(ROW * i, ROW * (i + 1));
        }

#if SHOW_UNDISTORTION 
        trackerData[i].showUndistortion("undistrotion_" + std::to_string(i)); // SHOW_UNDISTORITION�� 0���� �����Ͽ��⿡ ȣ����� �ʴ� �κ�, ������ 1�� �����ϰ� �����ϸ� �ְ���� ���� �̹����� �� �� ����
#endif
    }

    for (unsigned int i = 0;; i++) // ���� loop
    {
        bool completed = false; // loop stop Flag
        for (int j = 0; j < NUM_OF_CAM; j++) // ī�޶� ����ŭ �ݺ�
            if (j != 1 || !STEREO_TRACK) // mono�� ���
                completed |= trackerData[j].updateID(i); // feature�� index�� ������Ŵ
        if (!completed)
            break; // feature index ���� �� ���� ����
    }

   if (PUB_THIS_FRAME) // ���� frame�� publish�ؾ��ϴ� ���
   {
        pub_count++; // publish Ƚ�� ����
        sensor_msgs::PointCloudPtr feature_points(new sensor_msgs::PointCloud);
        sensor_msgs::ChannelFloat32 id_of_point;
        sensor_msgs::ChannelFloat32 u_of_point;
        sensor_msgs::ChannelFloat32 v_of_point;
        sensor_msgs::ChannelFloat32 velocity_x_of_point;
        sensor_msgs::ChannelFloat32 velocity_y_of_point;

        feature_points->header = img_msg->header; // feature_points�� header�� frame�� header�� ����
        feature_points->header.frame_id = "world";

        vector<set<int>> hash_ids(NUM_OF_CAM);
        for (int i = 0; i < NUM_OF_CAM; i++) // ī�޶� ���� ��ŭ loop
        {
            auto &un_pts = trackerData[i].cur_un_pts; // ���� undistorted feature point �迭
            auto &cur_pts = trackerData[i].cur_pts; // ���� feature point �迭
            auto &ids = trackerData[i].ids; // ���� feature�� index
            auto &pts_velocity = trackerData[i].pts_velocity; // ���� frame�� �ӵ�
            for (unsigned int j = 0; j < ids.size(); j++)
            {
                if (trackerData[i].track_cnt[j] > 1) // tracking count�� 1���� ũ�ٸ�
                {
                    int p_id = ids[j]; // feature index ����
                    hash_ids[i].insert(p_id); // index�� hash�� ����
                    geometry_msgs::Point32 p;
                    p.x = un_pts[j].x; // feature point�� ����
                    p.y = un_pts[j].y;
                    p.z = 1;

                    feature_points->points.push_back(p); // feature point�� �迭�� ����
                    id_of_point.values.push_back(p_id * NUM_OF_CAM + i); // feature index�� ī�޶� ���� ���Ͽ� �迭�� ����
                    u_of_point.values.push_back(cur_pts[j].x); // feature�� x, y �� ����
                    v_of_point.values.push_back(cur_pts[j].y);
                    velocity_x_of_point.values.push_back(pts_velocity[j].x); // frame�� �ӵ� ����
                    velocity_y_of_point.values.push_back(pts_velocity[j].y);
                }
            }
        }
        feature_points->channels.push_back(id_of_point); // ������ �迭�� publish�ϱ� ���� ��ü�� ����
        feature_points->channels.push_back(u_of_point);
        feature_points->channels.push_back(v_of_point);
        feature_points->channels.push_back(velocity_x_of_point);
        feature_points->channels.push_back(velocity_y_of_point);
        ROS_DEBUG("publish %f, at %f", feature_points->header.stamp.toSec(), ros::Time::now().toSec());
        // skip the first image; since no optical speed on frist image
        if (!init_pub) // publish�� ����� ù�� ° �̹����� ���
        {
            init_pub = 1; // publish�� �ʱ�ȭ �Ǿ��ٰ� ����
        }
        else
            pub_img.publish(feature_points); // ������ ��ü�� publish

        if (SHOW_TRACK) // config���Ͽ��� SHOW_TRACK�� 1�� �� ���
        {
            ptr = cv_bridge::cvtColor(ptr, sensor_msgs::image_encodings::BGR8); // ROS �̹��� �޽����� opencv�� BGR ���� �̹����� ��ȯ
            //cv::Mat stereo_img(ROW * NUM_OF_CAM, COL, CV_8UC3);
            cv::Mat stereo_img = ptr->image; // Mat ��ü�� frame ����

            for (int i = 0; i < NUM_OF_CAM; i++)
            {
                cv::Mat tmp_img = stereo_img.rowRange(i * ROW, (i + 1) * ROW); // tmp_img�� ���� ī�޶��� ������ �����Ͽ� ����
                cv::cvtColor(show_img, tmp_img, CV_GRAY2RGB); // show_img �̹����� GRAY���� BGR �������� ��ȯ�Ͽ� tmp_img�� ����

                for (unsigned int j = 0; j < trackerData[i].cur_pts.size(); j++) // ���� freature point �迭 ũ�⸸ŭ loop
                {
                    double len = std::min(1.0, 1.0 * trackerData[i].track_cnt[j] / WINDOW_SIZE); // tracking count/ WINDOW_SIZE�� 1�� ���� ���� len���� ����
                    cv::circle(tmp_img, trackerData[i].cur_pts[j], 2, cv::Scalar(255 * (1 - len), 0, 255 * len), 2); // feature�� ������ �ð�ȭ
                    //draw speed line
                    /*
                    Vector2d tmp_cur_un_pts (trackerData[i].cur_un_pts[j].x, trackerData[i].cur_un_pts[j].y);
                    Vector2d tmp_pts_velocity (trackerData[i].pts_velocity[j].x, trackerData[i].pts_velocity[j].y);
                    Vector3d tmp_prev_un_pts;
                    tmp_prev_un_pts.head(2) = tmp_cur_un_pts - 0.10 * tmp_pts_velocity;
                    tmp_prev_un_pts.z() = 1;
                    Vector2d tmp_prev_uv;
                    trackerData[i].m_camera->spaceToPlane(tmp_prev_un_pts, tmp_prev_uv);
                    cv::line(tmp_img, trackerData[i].cur_pts[j], cv::Point2f(tmp_prev_uv.x(), tmp_prev_uv.y()), cv::Scalar(255 , 0, 0), 1 , 8, 0);
                    */
                    //char name[10];
                    //sprintf(name, "%d", trackerData[i].ids[j]);
                    //cv::putText(tmp_img, name, trackerData[i].cur_pts[j], cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
                }
            }
            //cv::imshow("vis", stereo_img);
            //cv::waitKey(5);
            pub_match.publish(ptr->toImageMsg()); // �ð�ȭ�� �̹����� publish
        }
    }
    ROS_INFO("whole feature tracker processing costs: %f", t_r.toc());
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "feature_tracker"); // ROS node�� ��� (�����带 ����ٰ� ����)
    ros::NodeHandle n("~"); //ROS �Ű����� ������ ���� namesapce�κ��� �Ű� ������ ������ (config���� ����, vins_folder ����)
    ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Info); //ROS �α� ���� ���� (���α׷��� ���� �߿� �߻��ϴ� �̺�Ʈ�� ���� ������ ���, info�� �Ϲ����� ���� ����)
    readParameters(n); //�Ű������� �д´�. (feature_tracker�� paramerter.cpp�� �ִ� readParameters �Լ� ȣ��)

    for (int i = 0; i < NUM_OF_CAM; i++) //�ܾ��̹Ƿ� loop�� 1ȸ�� �ݺ���
        trackerData[i].readIntrinsicParameter(CAM_NAMES[i]); //config ������ �о� FeatureTracker�� Ʈ��ŷ�� ���� �Ķ���͸� ������

    if(FISHEYE) //FishEye ī�޶��� ���
    {
        for (int i = 0; i < NUM_OF_CAM; i++)
        {
            trackerData[i].fisheye_mask = cv::imread(FISHEYE_MASK, 0);
            if(!trackerData[i].fisheye_mask.data)
            {
                ROS_INFO("load mask fail");
                ROS_BREAK();
            }
            else
                ROS_INFO("load mask success");
        }
    }
    // /cam0/image_raw ��� ������ �����ϰ�, �޽��� ť ũ��� 100���� ����,���ο� �޽����� ���� img_callback�Լ��� ȣ��(callback�Լ�)
    ros::Subscriber sub_img = n.subscribe(IMAGE_TOPIC, 100, img_callback); 

    pub_img = n.advertise<sensor_msgs::PointCloud>("feature", 1000); //feature���� publish�� ������ ����(�޴� ���� /feature_tracker/feature), �޽��� ť ũ��� 1000���� ����. �̶� �޽��� ������ sensor_msgs::PointCloud�̴�.
    pub_match = n.advertise<sensor_msgs::Image>("feature_img",1000); //feature_img���� publish�� ������ ����, �޽��� ť ũ��� 1000���� ����. �̶� �޽��� ������ sensor_msgs::Image�̴�.
    pub_restart = n.advertise<std_msgs::Bool>("restart",1000); //restart���� publish�� ������ ����, �޽��� ť ũ��� 1000���� ����. �̶� �޽��� ������ std_msgs::Bool�̴�.
    /*
    if (SHOW_TRACK)
        cv::namedWindow("vis", cv::WINDOW_NORMAL);
    */
    ros::spin(); //���α׷��� ����� �� ���� �ݹ� �Լ� ó���� ��ٸ�
    return 0;
}


// new points velocity is 0, pub or not?
// track cnt > 1 pub?
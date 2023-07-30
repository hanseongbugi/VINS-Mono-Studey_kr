#include "parameters.h"

std::string IMAGE_TOPIC;
std::string IMU_TOPIC;
std::vector<std::string> CAM_NAMES;
std::string FISHEYE_MASK;
int MAX_CNT;
int MIN_DIST;
int WINDOW_SIZE;
int FREQ;
double F_THRESHOLD;
int SHOW_TRACK;
int STEREO_TRACK;
int EQUALIZE;
int ROW;
int COL;
int FOCAL_LENGTH;
int FISHEYE;
bool PUB_THIS_FRAME;

template <typename T>
T readParam(ros::NodeHandle &n, std::string name)
{
    T ans;
    if (n.getParam(name, ans)) //Node Handle���� name ���� ���� �Ķ���͸� ������ ans�� ����
    {
        ROS_INFO_STREAM("Loaded " << name << ": " << ans); //�α׿� ans�� ��� (�ַ� ���� ���)
    }
    else
    {
        ROS_ERROR_STREAM("Failed to load " << name);
        n.shutdown();
    }
    return ans;
}

void readParameters(ros::NodeHandle &n)
{
    std::string config_file; //config_file�� �̸��� ���� ����
    config_file = readParam<std::string>(n, "config_file"); //config_file�� ���õ� ���ڸ� �д´�. (euroc_config.yaml Ȥ�� euroc_config_no_extrinsic.yaml)
    cv::FileStorage fsSettings(config_file, cv::FileStorage::READ); //������ �д´�.
    if(!fsSettings.isOpened()) //������ �������� �ʴ´ٸ�
    {
        std::cerr << "ERROR: Wrong path to settings" << std::endl;
    }
    std::string VINS_FOLDER_PATH = readParam<std::string>(n, "vins_folder"); //vins_folder ��� ����

    fsSettings["image_topic"] >> IMAGE_TOPIC; //�̹����� ���� ���� (cam0/image_raw)
    fsSettings["imu_topic"] >> IMU_TOPIC; // imu�� ���� ���� (imu0/)
    MAX_CNT = fsSettings["max_cnt"]; //tracking�� ����Ǵ� �ִ� feature �� (150) 
    MIN_DIST = fsSettings["min_dist"]; //2���� feature �ּ� �Ÿ� (30) 
    ROW = fsSettings["image_height"]; //�̹����� row
    COL = fsSettings["image_width"]; //�̹����� col
    FREQ = fsSettings["freq"]; //tracking �� �� (10HZ)
    F_THRESHOLD = fsSettings["F_threshold"]; //RANSAC threshold �� (1 pixel)
    SHOW_TRACK = fsSettings["show_track"]; // tracking �� �̹����� Ȯ���� �������� ���� �÷���
    EQUALIZE = fsSettings["equalize"]; //�̹����� �ʹ� ��ų� ��ο� ��츦 ���� �÷���
    FISHEYE = fsSettings["fisheye"]; //�ǽþ��� ī�޶� ��� ��
    if (FISHEYE == 1)
        FISHEYE_MASK = VINS_FOLDER_PATH + "config/fisheye_mask.jpg";
    CAM_NAMES.push_back(config_file); //config_file �̸� �迭�� ����

    WINDOW_SIZE = 20; //window ũ�� ����
    STEREO_TRACK = false; //���׷����� ��� (mono�̹Ƿ� �ʿ���� ����)
    FOCAL_LENGTH = 460;
    PUB_THIS_FRAME = false;

    if (FREQ == 0)
        FREQ = 100;

    fsSettings.release();


}

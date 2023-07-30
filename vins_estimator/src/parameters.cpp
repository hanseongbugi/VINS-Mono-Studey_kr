#include "parameters.h"

double INIT_DEPTH;
double MIN_PARALLAX;
double ACC_N, ACC_W;
double GYR_N, GYR_W;

std::vector<Eigen::Matrix3d> RIC;
std::vector<Eigen::Vector3d> TIC;

Eigen::Vector3d G{0.0, 0.0, 9.8};

double BIAS_ACC_THRESHOLD;
double BIAS_GYR_THRESHOLD;
double SOLVER_TIME;
int NUM_ITERATIONS;
int ESTIMATE_EXTRINSIC;
int ESTIMATE_TD;
int ROLLING_SHUTTER;
std::string EX_CALIB_RESULT_PATH;
std::string VINS_RESULT_PATH;
std::string IMU_TOPIC;
double ROW, COL;
double TD, TR;

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

    fsSettings["imu_topic"] >> IMU_TOPIC; //bag������ imu topic�� ������ (/imu0)

    SOLVER_TIME = fsSettings["max_solver_time"]; //�ִ� solver �ݺ� �ð��� ������ (ms ����)
    NUM_ITERATIONS = fsSettings["max_num_iterations"]; //�ִ� solver �ݺ� Ƚ���� ������ (8ȸ)
    MIN_PARALLAX = fsSettings["keyframe_parallax"]; //keyFrame ������ ���� threshold �� (10 pixel)
    MIN_PARALLAX = MIN_PARALLAX / FOCAL_LENGTH; // threshold ���� 460(���� �Ÿ�)�� ������. (��� ���Ͽ� ���� ���ǵǾ� ����)

    std::string OUTPUT_PATH;
    fsSettings["output_path"] >> OUTPUT_PATH; // �� ������ ����� ���͸�
    VINS_RESULT_PATH = OUTPUT_PATH + "/vins_result_no_loop.csv"; //vins ���� �� ����� ����
    std::cout << "result path " << VINS_RESULT_PATH << std::endl; //����� ������ ��� ���

    // create folder if not exists
    FileSystemHelper::createDirectoryIfNotExists(OUTPUT_PATH.c_str()); //���͸� ���� (���ٸ�)

    std::ofstream fout(VINS_RESULT_PATH, std::ios::out); //csv ������ ����
    fout.close();

    ACC_N = fsSettings["acc_n"]; //���ӵ� noise
    ACC_W = fsSettings["acc_w"]; //���ӵ� bias
    GYR_N = fsSettings["gyr_n"]; //���̷� noise
    GYR_W = fsSettings["gyr_w"]; //���̷� bias
    G.z() = fsSettings["g_norm"]; // �߷� ���ӵ�
    ROW = fsSettings["image_height"]; //�̹����� row�� ������
    COL = fsSettings["image_width"]; //�̹����� col�� ������
    ROS_INFO("ROW: %f COL: %f ", ROW, COL); //�̹����� row�� col�� ROS �α� �޽����� ���� ��� ([ INFO] : ROW : COL : �������� ��µ�)

    ESTIMATE_EXTRINSIC = fsSettings["estimate_extrinsic"]; //IMU�� camera ������ T����� ������ ������ ���� ������
    if (ESTIMATE_EXTRINSIC == 2) // �ʱ� �� ���� ������ ������ (��ȯ ����� ����)
    {
        ROS_WARN("have no prior about extrinsic param, calibrate extrinsic param");
        RIC.push_back(Eigen::Matrix3d::Identity()); //���� ���(3x3)�� ������ �迭�� ����
        TIC.push_back(Eigen::Vector3d::Zero()); //�� ���(3x1)�� �����Ͽ� �迭�� ����
        EX_CALIB_RESULT_PATH = OUTPUT_PATH + "/extrinsic_parameter.csv"; //���� ����� ����� ���� ��� ����

    }
    else 
    {
        if ( ESTIMATE_EXTRINSIC == 1) // yaml ���Ͽ� �ִ� ���� ���� ������ ��
        {
            ROS_WARN(" Optimize extrinsic param around initial guess!");
            EX_CALIB_RESULT_PATH = OUTPUT_PATH + "/extrinsic_parameter.csv"; //���� ����� ����� ���� ��� ����
        }
        if (ESTIMATE_EXTRINSIC == 0) // yaml ���Ͽ� �ִ� ���� �״�� ���
            ROS_WARN(" fix extrinsic param ");

        cv::Mat cv_R, cv_T;
        fsSettings["extrinsicRotation"] >> cv_R; //imu ȸ�� ��ȯ ����� ������
        fsSettings["extrinsicTranslation"] >> cv_T; //imu �̵� ��ȯ ����� ������
        Eigen::Matrix3d eigen_R;
        Eigen::Vector3d eigen_T;
        cv::cv2eigen(cv_R, eigen_R); //opencv�� Mat ��ü�� eigen3�� Matrix3d ��ü�� ��ȯ
        cv::cv2eigen(cv_T, eigen_T); //opencv�� Mat ��ü�� eigen3�� Vector3d ��ü�� ��ȯ
        Eigen::Quaterniond Q(eigen_R); //ȸ�� ����� ���� ���ʹϾ����� ��ȯ
        eigen_R = Q.normalized(); //���ʹϾ� ���� normalized
        RIC.push_back(eigen_R); //�迭�� ����
        TIC.push_back(eigen_T);
        ROS_INFO_STREAM("Extrinsic_R : " << std::endl << RIC[0]); //�α׸� ���� ����� ���
        ROS_INFO_STREAM("Extrinsic_T : " << std::endl << TIC[0].transpose());
        
    } 

    INIT_DEPTH = 5.0;
    BIAS_ACC_THRESHOLD = 0.1;
    BIAS_GYR_THRESHOLD = 0.1;

    TD = fsSettings["td"]; //imu �ð��� �̹��� �ð����� ���� ��
    ESTIMATE_TD = fsSettings["estimate_td"]; //�� ���� 1�̸� ���� ���� ��� (imu�� ī�޶��� �ð� ����ȭ�� �Ǿ� ���� �ʴ� ���)
    if (ESTIMATE_TD)
        ROS_INFO_STREAM("Unsynchronized sensors, online estimate time offset, initial td: " << TD);
    else
        ROS_INFO_STREAM("Synchronized sensors, fix time offset: " << TD);

    ROLLING_SHUTTER = fsSettings["rolling_shutter"]; //�Ѹ� ���� ī�޶��� ���
    if (ROLLING_SHUTTER)
    {
        TR = fsSettings["rolling_shutter_tr"];
        ROS_INFO_STREAM("rolling shutter camera, read out time per line: " << TR);
    }
    else
    {
        TR = 0;
    }
    
    fsSettings.release();
}

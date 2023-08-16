#include "estimator.h"

Estimator::Estimator(): f_manager{Rs}
{
    ROS_INFO("init begins"); 
    clearState(); // ���� �ʱ�ȭ
}

void Estimator::setParameter()
{
    for (int i = 0; i < NUM_OF_CAM; i++)
    {
        tic[i] = TIC[i]; //�Ķ���ͷ� ���� ��ȯ ����� ����
        ric[i] = RIC[i];
    }
    f_manager.setRic(ric); //feature manager ��ü�� ric �迭 ����
    ProjectionFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Matrix2d::Identity(); //projection factor�� ���ϰ� ���� ��Ŀ� ���� ����
    ProjectionTdFactor::sqrt_info = FOCAL_LENGTH / 1.5 * Matrix2d::Identity(); 
    td = TD; //imu �ð��� �̹��� �ð����� ���� �� ����
}

void Estimator::clearState()
{
    for (int i = 0; i < WINDOW_SIZE + 1; i++)
    {
        Rs[i].setIdentity();
        Ps[i].setZero();
        Vs[i].setZero();
        Bas[i].setZero();
        Bgs[i].setZero();
        dt_buf[i].clear();
        linear_acceleration_buf[i].clear();
        angular_velocity_buf[i].clear();

        if (pre_integrations[i] != nullptr)
            delete pre_integrations[i];
        pre_integrations[i] = nullptr;
    }

    for (int i = 0; i < NUM_OF_CAM; i++)
    {
        tic[i] = Vector3d::Zero();
        ric[i] = Matrix3d::Identity();
    }

    for (auto &it : all_image_frame)
    {
        if (it.second.pre_integration != nullptr)
        {
            delete it.second.pre_integration;
            it.second.pre_integration = nullptr;
        }
    }

    solver_flag = INITIAL;
    first_imu = false,
    sum_of_back = 0;
    sum_of_front = 0;
    frame_count = 0;
    solver_flag = INITIAL;
    initial_timestamp = 0;
    all_image_frame.clear();
    td = TD;


    if (tmp_pre_integration != nullptr)
        delete tmp_pre_integration;
    if (last_marginalization_info != nullptr)
        delete last_marginalization_info;

    tmp_pre_integration = nullptr;
    last_marginalization_info = nullptr;
    last_marginalization_parameter_blocks.clear();

    f_manager.clearState();

    failure_occur = 0;
    relocalization_info = 0;

    drift_correct_r = Matrix3d::Identity();
    drift_correct_t = Vector3d::Zero();
}

void Estimator::processIMU(double dt, const Vector3d &linear_acceleration, const Vector3d &angular_velocity)
{
    if (!first_imu) // ù��° imu �����͸� ���� ���� ���
    {
        first_imu = true; // ù��° imu �����͸� �޾Ҵٰ� flag ����
        acc_0 = linear_acceleration; // ���ӵ� ���� ���ڷ� ���� ������ ����
        gyr_0 = angular_velocity; // ���̷� ���� ���ڷ� ���� ������ ����
    }

    if (!pre_integrations[frame_count]) // frame ��ȣ�� �ش��ϴ� preIntegration ��ü�� ���� ���
    {
        pre_integrations[frame_count] = new IntegrationBase{acc_0, gyr_0, Bas[frame_count], Bgs[frame_count]}; // IntegrationBase ��ü ���� (Integration�� �ʿ��� ��� �ʱ�ȭ)
    }
    if (frame_count != 0) // frame�� �ִ� ���
    {
        pre_integrations[frame_count]->push_back(dt, linear_acceleration, angular_velocity); // dt�� ���ӵ�, ���̷� ���� ���� ���� preintegration ����
        //if(solver_flag != NON_LINEAR)
            tmp_pre_integration->push_back(dt, linear_acceleration, angular_velocity); // ���� ������ �ѹ��� integration ����

        dt_buf[frame_count].push_back(dt); // dt ���ۿ� dt �� ����
        linear_acceleration_buf[frame_count].push_back(linear_acceleration); // ���ӵ� ���ۿ� ���ӵ� �� ���� 
        angular_velocity_buf[frame_count].push_back(angular_velocity); // ���̷� ���ۿ� ���̷� �� ����

        int j = frame_count;         
        Vector3d un_acc_0 = Rs[j] * (acc_0 - Bas[j]) - g; //���� ������ ��ȣ�� ���� ���ӵ� ��
        Vector3d un_gyr = 0.5 * (gyr_0 + angular_velocity) - Bgs[j]; // ���� ������ ��ȣ�� ���� ���̷� ��
        Rs[j] *= Utility::deltaQ(un_gyr * dt).toRotationMatrix(); //ȸ�� ��� �� ���� (���̷� ���� ����)
        Vector3d un_acc_1 = Rs[j] * (linear_acceleration - Bas[j]) - g; // �۽ŵ� ȸ�� ��� ���� ���� ���� ���ӵ� �� ���
        Vector3d un_acc = 0.5 * (un_acc_0 + un_acc_1); // �� ���� ����� ����
        Ps[j] += dt * Vs[j] + 0.5 * dt * dt * un_acc; //��ġ ����
        Vs[j] += dt * un_acc; // �ӵ� ����
    }
    acc_0 = linear_acceleration; // ���ӵ� ���� ���ڷ� ���� ������ ����
    gyr_0 = angular_velocity; // ���̷� ���� ���ڷ� ���� ������ ����
}

void Estimator::processImage(const map<int, vector<pair<int, Eigen::Matrix<double, 7, 1>>>> &image, const std_msgs::Header &header)
{
    ROS_DEBUG("new image coming ------------------------------------------");
    ROS_DEBUG("Adding feature points %lu", image.size());
    if (f_manager.addFeatureCheckParallax(frame_count, image, td)) // frame ���� frame ����, td�� �ְ� Ư¡ �� �� parallax ���
        marginalization_flag = MARGIN_OLD; //marginalization flag�� OLD�� ���� (marginalization���� ���� �����͸� ���)
    else
        marginalization_flag = MARGIN_SECOND_NEW; //marginalization flag�� SECOND_NEW�� ���� (�ش� �������� Ű���������� ����)

    ROS_DEBUG("this frame is--------------------%s", marginalization_flag ? "reject" : "accept");
    ROS_DEBUG("%s", marginalization_flag ? "Non-keyframe" : "Keyframe");
    ROS_DEBUG("Solving %d", frame_count);
    ROS_DEBUG("number of feature: %d", f_manager.getFeatureCount()); // Ư¡�� ���� ��ȯ
    Headers[frame_count] = header; //frame_count��° �̹��� ������ Headers �迭�� ����

    ImageFrame imageframe(image, header.stamp.toSec()); //Ư¡�� ������ timeStamp�� ���� ImageFrame ��ü ����
    imageframe.pre_integration = tmp_pre_integration; // ImageFrame ��ü�� preIntegration ���� ���� (processIMU �Լ��� ���� ���� integration ��)
    all_image_frame.insert(make_pair(header.stamp.toSec(), imageframe)); // ��� �̹��� �������� ��� map ��ü�� time stamp�� �̶� �ش��ϴ� ImageFrame ��ü ����
    tmp_pre_integration = new IntegrationBase{acc_0, gyr_0, Bas[frame_count], Bgs[frame_count]}; // frame count�� �ش��ϴ� bias�� ���� integration ����

    if(ESTIMATE_EXTRINSIC == 2) // Imu�� camera�� ��ȯ ����� �����ؾ� �Ѵٸ�
    {
        ROS_INFO("calibrating extrinsic param, rotation movement is needed");
        if (frame_count != 0) // �������� ���Դٸ�
        {
            vector<pair<Vector3d, Vector3d>> corres = f_manager.getCorresponding(frame_count - 1, frame_count); // ���� ������ ��ȣ�� ���� ������ ��ȣ�� ���� corespondent�� Ư¡���� ����
            Matrix3d calib_ric;
            // corespondent �迭�� imu ȸ�� ����� ���� RIC ��� ����
            if (initial_ex_rotation.CalibrationExRotation(corres, pre_integrations[frame_count]->delta_q, calib_ric)) 
            {
                ROS_WARN("initial extrinsic rotation calib success");
                ROS_WARN_STREAM("initial extrinsic rotation: " << endl << calib_ric);
                ric[0] = calib_ric; // ������ RIC ����
                RIC[0] = calib_ric;
                ESTIMATE_EXTRINSIC = 1; // ���� frag ���� 1�� ���� (������ RIC�� ����ȭ�� �����ϱ� ����) 
            }
        }
    }

    if (solver_flag == INITIAL) // solver flag�� �ʱ�ȭ ���¶��
    {
        if (frame_count == WINDOW_SIZE) // frame count�� WINDOW_SIZE(10)�� ���ٸ�
        {
            bool result = false; // ��� �� �ʱ�ȭ
            // Imu�� camera�� ��ȯ ����� �����ؾ��ϴ� ���°� �ƴϰ� frame�� time stamp - �ʱ� time stamp���� 0.1���� ũ��
            if( ESTIMATE_EXTRINSIC != 2 && (header.stamp.toSec() - initial_timestamp) > 0.1)
            {
               result = initialStructure(); // VINS-MONO�� �ʿ��� �ڷ� ���� �ʱ�ȭ
               initial_timestamp = header.stamp.toSec(); // �ʱ� time stamp ���� ���� frame�� time stamp�� �ʱ�ȭ
            }
            if(result) // �ʱ�ȭ ���� ��
            {
                solver_flag = NON_LINEAR; // solver flag�� NON_LINEAR�� ����
                solveOdometry(); // triangulation �� optimization ����
                slideWindow(); // slideWindow ����
                f_manager.removeFailures(); // solve_flag�� 2�� feature�� �迭���� ����
                ROS_INFO("Initialization finish!");
                last_R = Rs[WINDOW_SIZE]; // ���� R�� P�� ����
                last_P = Ps[WINDOW_SIZE];
                last_R0 = Rs[0]; // ���� R0�� P0�� ����
                last_P0 = Ps[0];
                
            }
            else // �ʱ�ȭ ����
                slideWindow(); // slideWindow ����
        }
        else
            frame_count++; // frame count ����
    }
    else // solver flag�� NON_LINEAR���
    {
        TicToc t_solve; // ���� �ð� ����
        solveOdometry(); // triangulation �� optimization ����
        ROS_DEBUG("solver costs: %fms", t_solve.toc());

        if (failureDetection()) // failure ������ �����Ͽ��ٸ�
        {
            ROS_WARN("failure detection!");
            failure_occur = 1; // failure�� �߻��Ͽ��ٰ� flag ����
            clearState(); // ���� �ʱ�ȭ
            setParameter(); // �Ķ���� �ʱ�ȭ
            ROS_WARN("system reboot!");
            return;
        }

        TicToc t_margin; // ���� �ð� ����
        slideWindow(); // slideWindow ����
        f_manager.removeFailures(); // solve_flag�� 2�� feature�� �迭���� ����
        ROS_DEBUG("marginalization costs: %fms", t_margin.toc());
        // prepare output of VINS
        key_poses.clear(); // key_poses �迭�� ��� ��� ����
        for (int i = 0; i <= WINDOW_SIZE; i++)
            key_poses.push_back(Ps[i]); // ī�޶� pose(WINDOW_SIZE ��ŭ)�� key_pose�� �Ѵ�

        last_R = Rs[WINDOW_SIZE]; // ���� R�� P�� ����
        last_P = Ps[WINDOW_SIZE];
        last_R0 = Rs[0];
        last_P0 = Ps[0]; // ���� R0�� P0�� ����
    }
}
bool Estimator::initialStructure()
{
    TicToc t_sfm; // ���� �ð��� ����ϴ� ��ü ����
    //check imu observibility
    {
        map<double, ImageFrame>::iterator frame_it; // map<double, ImageFrame> ������ map ��ü�� iterator ����
        Vector3d sum_g; // ���ӵ� ���� ��
        for (frame_it = all_image_frame.begin(), frame_it++; frame_it != all_image_frame.end(); frame_it++) // ������ ��� frame�� ���ؼ� ��ȸ
        {
            double dt = frame_it->second.pre_integration->sum_dt; // ���� iterator�� ����Ű�� integration��ü�� dt ���� dt�� ����
            Vector3d tmp_g = frame_it->second.pre_integration->delta_v / dt; // ���� �ӵ��� dt�� ���� ���ӵ� ���� ����
            sum_g += tmp_g; // ���ӵ� ���� ���� ����
        }
        Vector3d aver_g;
        aver_g = sum_g * 1.0 / ((int)all_image_frame.size() - 1); // ���ӵ� ���� ����� ���Ѵ�,
        double var = 0; // ���ӵ� ������ �л� ��
        for (frame_it = all_image_frame.begin(), frame_it++; frame_it != all_image_frame.end(); frame_it++) // ������ ��� frame�� ���ؼ� ��ȸ
        {
            double dt = frame_it->second.pre_integration->sum_dt; // ���� iterator�� ����Ű�� integration��ü�� dt ���� dt�� ����
            Vector3d tmp_g = frame_it->second.pre_integration->delta_v / dt; // ���� �ӵ��� dt�� ���� ���ӵ� ���� ����

            // ��� ImageFrame ��ü���� ���ӵ� ���� ��� ���ӵ��� ���̸� �����Ͽ� ��� ���� ����� var�� ����ȴ�.
            var += (tmp_g - aver_g).transpose() * (tmp_g - aver_g); 
            //cout << "frame g " << tmp_g.transpose() << endl;
        }
        var = sqrt(var / ((int)all_image_frame.size() - 1));  // �л� ���� frame�� ������ ������ ǥ�������� ��ȯ
        //ROS_WARN("IMU variation %f!", var);
        if(var < 0.25) // ǥ�������� 0.25���� ������
        {
            ROS_INFO("IMU excitation not enouth!"); 
            //return false;
        }
    }
    // global sfm
    Quaterniond Q[frame_count + 1]; //frame count ��ŭ ī�޶��� ȸ���� ��Ÿ���� �迭�� ����
    Vector3d T[frame_count + 1]; //frame count ��ŭ ī�޶��� ��ġ�� ��Ÿ���� �迭�� ����
    map<int, Vector3d> sfm_tracked_points; // ������ ���� ��ǥ�� �����ϴ� map ��ü ����
    vector<SFMFeature> sfm_f; // SFMFeature ��ü�� �����ϴ� �迭 ����
    for (auto &it_per_id : f_manager.feature) // FeatureManager ��ü�� feature �迭�� ��ȸ
    {
        int imu_j = it_per_id.start_frame - 1; //iterator�� ����Ű�� Ư¡���� ���� ������ �ε���
        SFMFeature tmp_feature; //SFMFeature ��ü ����
        tmp_feature.state = false; // ���¸� false�� �ʱ�ȭ
        tmp_feature.id = it_per_id.feature_id; // id�� feature_id�� �ʱ�ȭ
        for (auto &it_per_frame : it_per_id.feature_per_frame) //iterator�� ����Ű�� feature_per_frame �迭 ��ȸ
        {
            imu_j++; //Ư¡���� ���� ������ �ε��� ����
            Vector3d pts_j = it_per_frame.point; // Ư¡�� ��ǥ�� �޴´�.
            tmp_feature.observation.push_back(make_pair(imu_j, Eigen::Vector2d{pts_j.x(), pts_j.y()})); // SFMFeature�� observation �迭�� �ε����� Ư¡�� ��ǥ ����
        }
        sfm_f.push_back(tmp_feature); // SFMFeature ��ü�� �迭�� ����
    } 
    Matrix3d relative_R; //��� ȸ�� ���
    Vector3d relative_T; //��� ��ġ ���
    int l; // �� ������ ������ ��� frame �ε���
    if (!relativePose(relative_R, relative_T, l)) // ��� ȸ�� ��İ� ��ġ ����� ������ ���Ͽ��ٸ�
    {
        ROS_INFO("Not enough features or parallax; Move device around");
        return false; // ����
    }
    GlobalSFM sfm; //GlobalSFM ��ü�� ����
    if(!sfm.construct(frame_count + 1, Q, T, l,
              relative_R, relative_T,
              sfm_f, sfm_tracked_points)) // PnP, triangulate, Bundle Adjustment�� ���� point�� �����Ͽ� sfm_tracked_points�� ����, �̶� 3���� ���� �� �ϳ��� ���� ��
    {
        ROS_DEBUG("global SFM failed!");
        marginalization_flag = MARGIN_OLD; //marginalization flag�� OLD�� ���� (marginalization���� ���� �����͸� ���)
        return false; // ����
    }

    //solve pnp for all frame
    map<double, ImageFrame>::iterator frame_it; // <double, ImageFrame>������ map ��ü�� iterator ����
    map<int, Vector3d>::iterator it; // <int, Vector3d>������ map ��ü�� iterator ����
    frame_it = all_image_frame.begin( ); // ��� �̹��� �������� ����ִ� map ��ü�� ù ��Ҹ� ����Ű�� �Ѵ�.
    for (int i = 0; frame_it != all_image_frame.end( ); frame_it++) // iterator�� �̿��� ��� �̹��� �������� ����ִ� map ��ü ��ȸ
    {
        // provide initial guess
        cv::Mat r, rvec, t, D, tmp_r;
        if((frame_it->first) == Headers[i].stamp.toSec()) // ���� ����Ű�� frame�� time stamp�� Headers �迭�� �����Ѵٸ�
        {
            frame_it->second.is_key_frame = true; // ���� ����Ű�� frame�� keyFrame���� �Ѵ�.
            frame_it->second.R = Q[i].toRotationMatrix() * RIC[0].transpose(); // ī�޶� Rotation ��İ� IMU Rotation ����� ���Ͽ� Frame Rotation ��ķ� �ʱ�ȭ
            frame_it->second.T = T[i]; // ī�޶� Translation ����� Frame Translation ��ķ� �ʱ�ȭ
            i++; // i �� ����
            continue; // �Ʒ� ���� (PnP ���� ����)
        }
        if((frame_it->first) > Headers[i].stamp.toSec())// ���� ����Ű�� frame�� time stamp�� Headers �迭�� i��° time stamp���� ũ�ٸ�
        {
            i++; // i �� ����
        }
        Matrix3d R_inital = (Q[i].inverse()).toRotationMatrix(); // ī�޶� Rotation ����� ������� ���ʹϾȿ��� Matrix3d ��ü�� ��ȯ�Ͽ� R_inital��� ����
        Vector3d P_inital = - R_inital * T[i]; // R_inital ��Ŀ� ������ ���ϰ� ī�޶� Translation ����� ���Ͽ� P_inital ��� ����
        cv::eigen2cv(R_inital, tmp_r); // R_inital ����� opencv�� Mat ��ü�� ����
        cv::Rodrigues(tmp_r, rvec); // ȸ�� ����� Rodrigues ��ȯ
        cv::eigen2cv(P_inital, t); // P_inital ����� opencv�� Mat ��ü�� ����

        frame_it->second.is_key_frame = false; // ���� ����Ű�� frame�� keyFrame�� �ƴ϶�� ����
        vector<cv::Point3f> pts_3_vector; 
        vector<cv::Point2f> pts_2_vector;
        for (auto &id_pts : frame_it->second.points) // feature�� ������ŭ ��ȸ
        {
            int feature_id = id_pts.first; // feature id�� ������
            for (auto &i_p : id_pts.second) //feature�� position�� �������� ���� �迭 ��ȸ
            {
                it = sfm_tracked_points.find(feature_id); 
                if(it != sfm_tracked_points.end()) // Global SFM�� ���� ������ point �� feature_id�� ���� ���� �ִٸ� 
                {
                    Vector3d world_pts = it->second; // ������ Point ��ǥ�� ���
                    cv::Point3f pts_3(world_pts(0), world_pts(1), world_pts(2)); // opencv�� Point3f ��ü ����
                    pts_3_vector.push_back(pts_3); // �迭�� ����
                    Vector2d img_pts = i_p.second.head<2>(); // Ư¡���� �̹��� ���� ��ǥ�� ���
                    cv::Point2f pts_2(img_pts(0), img_pts(1));
                    pts_2_vector.push_back(pts_2); // �迭�� ����
                }
            }
        }
        cv::Mat K = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, 1, 0, 0, 0, 1); // ī�޶� �Ķ���� K ��� ����   
        if(pts_3_vector.size() < 6) // ������ Point ������ 6���� ���� ���
        {
            cout << "pts_3_vector size " << pts_3_vector.size() << endl;
            ROS_DEBUG("Not enough points for solve pnp !");
            return false;
        }
        if (! cv::solvePnP(pts_3_vector, pts_2_vector, K, D, rvec, t, 1)) // PnP�� ������ ���
        {
            ROS_DEBUG("solve pnp fail!");
            return false;
        }
        cv::Rodrigues(rvec, r); // Rodrigues ��ȯ�� Rotation ����� ���� ���·� ��ȯ
        MatrixXd R_pnp,tmp_R_pnp;
        cv::cv2eigen(r, tmp_R_pnp); // opencv�� Mat�� Eigen�� Matrix �������� ��ȯ
        R_pnp = tmp_R_pnp.transpose(); // Roation ��� ��ġ
        MatrixXd T_pnp;
        cv::cv2eigen(t, T_pnp); // opencv�� Mat�� Eigen�� Matrix �������� ��ȯ
        T_pnp = R_pnp * (-T_pnp); // Translation = ȸ�� ��� * Translation ��Ŀ� ������ ���� ��
        frame_it->second.R = R_pnp * RIC[0].transpose(); // Rotation ��Ŀ� Imu�� ī�޶� Rotation ����� ��ġ�Ͽ� ���Ͽ� �������� Rotation update
        frame_it->second.T = T_pnp; // Translation Update
    }
    if (visualInitialAlign()) // ī�޶� �� IMU ���� ���� ������Ʈ�� �����Ѵٸ� 
        return true;
    else // ���� ��
    {
        ROS_INFO("misalign visual structure with IMU");
        return false;
    }

}

bool Estimator::visualInitialAlign()
{
    TicToc t_g;
    VectorXd x;
    //solve scale
    bool result = VisualIMUAlignment(all_image_frame, Bgs, g, x); //IMU �����Ϳ� frame �����͸� �̿��Ͽ� �߷� ���� �� gyro bias ����
    if(!result) // ���� ���� ��
    {
        ROS_DEBUG("solve g failed!");
        return false;
    }

    // change state
    for (int i = 0; i <= frame_count; i++) // frame_count ��ŭ ��ȸ
    {
        Matrix3d Ri = all_image_frame[Headers[i].stamp.toSec()].R; // ��� frame���� i�� �ش��ϴ� �̹��� frame�� rotation ����� ������
        Vector3d Pi = all_image_frame[Headers[i].stamp.toSec()].T; // ��� frame���� i�� �ش��ϴ� �̹��� frame�� translation ����� ������
        Ps[i] = Pi; // ������ Translation ����� ī�޶� ��ġ��
        Rs[i] = Ri; // rotation ����� ī�޶� ȸ������ 
        all_image_frame[Headers[i].stamp.toSec()].is_key_frame = true; // �ش� �̹��� �������� Ű ���������� ����
    }

    VectorXd dep = f_manager.getDepthVector(); // feature�� depth �迭�� ������
    for (int i = 0; i < dep.size(); i++) // �迭�� ũ�⸸ŭ ��ȸ
        dep[i] = -1; // depth�� -1�� �ʱ�ȭ
    f_manager.clearDepth(dep); // depth �� ����

    //triangulat on cam pose , no tic
    Vector3d TIC_TMP[NUM_OF_CAM]; // ī�޶� ������ŭ TIC_TMP ��� �迭 ����
    for(int i = 0; i < NUM_OF_CAM; i++)
        TIC_TMP[i].setZero(); // TIC_TMP ����� 0��ķ� �ʱ�ȭ
    ric[0] = RIC[0]; // 0��°�� ���� RIC ����� ������
    f_manager.setRic(ric); // FeatureManager ��ü�� RIC ����
    f_manager.triangulate(Ps, &(TIC_TMP[0]), &(RIC[0])); // ��ġ�� TIC, RIC�� ���� triangulation ����

    double s = (x.tail<1>())(0); // scale�� �����´�. 
    for (int i = 0; i <= WINDOW_SIZE; i++) // WINDOW ũ�⸸ŭ ��ȸ
    {
        pre_integrations[i]->repropagate(Vector3d::Zero(), Bgs[i]); // bias�� ���� IMU ������ �纸��
    }
    for (int i = frame_count; i >= 0; i--) // frame_count�� ������ ��ȸ
        Ps[i] = s * Ps[i] - Rs[i] * TIC[0] - (s * Ps[0] - Rs[0] * TIC[0]); // ī�޶� ��ġ�� scale ���� ���� ����
    int kv = -1;
    map<double, ImageFrame>::iterator frame_i;
    for (frame_i = all_image_frame.begin(); frame_i != all_image_frame.end(); frame_i++) // ��� frame�� ���� ��ȸ
    {
        if(frame_i->second.is_key_frame) // keyFrame�� ���
        {
            kv++; // index����
            Vs[kv] = frame_i->second.R * x.segment<3>(kv * 3); // �ӵ��� keyFrame�� Rotation ����� x�� �ִ� �ӵ� ������ ���ؼ� �ӵ� ������ ���� 
        }
    }
    for (auto &it_per_id : f_manager.feature) // Ư¡�� �迭 ��ȸ
    {
        it_per_id.used_num = it_per_id.feature_per_frame.size(); // FeaturePerFrame ��ü�� ũ�⸦ used_num���� ����
        if (!(it_per_id.used_num >= 2 && it_per_id.start_frame < WINDOW_SIZE - 2)) // used_num�� 2���� �۰�, start_frame�� index�� WINDOW_SIZE - 2(8)���� ũ��
            continue; // ����
        it_per_id.estimated_depth *= s; // depth�� scale ũ�⸸ŭ ���ؼ� ����
    }

    Matrix3d R0 = Utility::g2R(g); // �߷� ���ӵ� ����� Rotation ��ķ� ��ȯ
    double yaw = Utility::R2ypr(R0 * Rs[0]).x(); // R0 * ī�޶� ȸ�� ���� yaw, ptich, row�� ��ȯ �� yaw ���� ������
    R0 = Utility::ypr2R(Eigen::Vector3d{-yaw, 0, 0}) * R0; // -yaw���� Rotation ��ķ� ��ȯ�ϰ� R0�� ���� R0 ����
    g = R0 * g; // Rotation ��İ� �߷� ���ӵ��� ���� �߷� ���ӵ� ����
    //Matrix3d rot_diff = R0 * Rs[0].transpose();
    Matrix3d rot_diff = R0; // Rotation ����� ����ġ ��ķ� ����
    for (int i = 0; i <= frame_count; i++) // frame_count ��ŭ ��ȸ
    {
        Ps[i] = rot_diff * Ps[i]; // ī�޶� ��ġ, ȸ��, �ӵ��� ����ġ�� ���� ����
        Rs[i] = rot_diff * Rs[i];
        Vs[i] = rot_diff * Vs[i];
    }
    ROS_DEBUG_STREAM("g0     " << g.transpose());
    ROS_DEBUG_STREAM("my R0  " << Utility::R2ypr(Rs[0]).transpose()); 

    return true;
}

bool Estimator::relativePose(Matrix3d &relative_R, Vector3d &relative_T, int &l)
{
    // find previous frame which contians enough correspondance and parallex with newest frame
    for (int i = 0; i < WINDOW_SIZE; i++) // WINDOW_SIZE(10) ��ŭ �ݺ�
    {
        vector<pair<Vector3d, Vector3d>> corres; 
        corres = f_manager.getCorresponding(i, WINDOW_SIZE); //FeatureManager ��ü���� i��° frame�� WINDOW ���� correspondent�� �����´�.
        if (corres.size() > 20) //�����ϴ� Ư¡�� ������ 20�� �Ѵ� ���
        {
            double sum_parallax = 0; // parallax�� �� �ʱ�ȭ
            double average_parallax; // parallax�� ���
            for (int j = 0; j < int(corres.size()); j++) // correspondent �迭�� ũ�⸸ŭ ��ȸ
            {
                Vector2d pts_0(corres[j].first(0), corres[j].first(1)); // correspondent�� ù��° point ��ǥ�� ������
                Vector2d pts_1(corres[j].second(0), corres[j].second(1)); // correspondent�� �ι�° point ��ǥ�� ������
                double parallax = (pts_0 - pts_1).norm(); // �� ���� ������ ���� ���� normalization�Ͽ�(�Ÿ��� ���Ͽ�) pararllax�� ���Ѵ�.
                sum_parallax = sum_parallax + parallax; // parallax�� �� ����

            }
            average_parallax = 1.0 * sum_parallax / int(corres.size()); // parallax ��� �� ���
            // ��� parallax�� 30���� ũ�� MotionEstimator ��ü(initial/solve_5pts.cpp)�� solveRelativeRT�� ���� ������� Rotation ��İ� Translation ��ĸ� ���� �ߴٸ�
            if(average_parallax * 460 > 30 && m_estimator.solveRelativeRT(corres, relative_R, relative_T))
            {
                l = i; // i���� l ������ ����
                ROS_DEBUG("average_parallax %f choose l %d and newest frame to triangulate the whole structure", average_parallax * 460, l);
                return true; //������ ����
            }
        }
    }
    return false; // ���� ����
}

void Estimator::solveOdometry()
{
    if (frame_count < WINDOW_SIZE) // frame_count�� WINDOW_SIZE���� ������ �Լ��� ����
        return;
    if (solver_flag == NON_LINEAR) // solver_flag�� NON_LINEAR�̸�
    {
        TicToc t_tri; 
        f_manager.triangulate(Ps, tic, ric); // ī�޶� ��ġ, Tic, Ric�� ���� triangulation ����
        ROS_DEBUG("triangulation costs %f", t_tri.toc()); // tritangulation ���� �ð� ���
        optimization(); // optimization ����
    }
}

void Estimator::vector2double()
{
    for (int i = 0; i <= WINDOW_SIZE; i++)
    {
        para_Pose[i][0] = Ps[i].x();
        para_Pose[i][1] = Ps[i].y();
        para_Pose[i][2] = Ps[i].z();
        Quaterniond q{Rs[i]};
        para_Pose[i][3] = q.x();
        para_Pose[i][4] = q.y();
        para_Pose[i][5] = q.z();
        para_Pose[i][6] = q.w();

        para_SpeedBias[i][0] = Vs[i].x();
        para_SpeedBias[i][1] = Vs[i].y();
        para_SpeedBias[i][2] = Vs[i].z();

        para_SpeedBias[i][3] = Bas[i].x();
        para_SpeedBias[i][4] = Bas[i].y();
        para_SpeedBias[i][5] = Bas[i].z();

        para_SpeedBias[i][6] = Bgs[i].x();
        para_SpeedBias[i][7] = Bgs[i].y();
        para_SpeedBias[i][8] = Bgs[i].z();
    }
    for (int i = 0; i < NUM_OF_CAM; i++)
    {
        para_Ex_Pose[i][0] = tic[i].x();
        para_Ex_Pose[i][1] = tic[i].y();
        para_Ex_Pose[i][2] = tic[i].z();
        Quaterniond q{ric[i]};
        para_Ex_Pose[i][3] = q.x();
        para_Ex_Pose[i][4] = q.y();
        para_Ex_Pose[i][5] = q.z();
        para_Ex_Pose[i][6] = q.w();
    }

    VectorXd dep = f_manager.getDepthVector();
    for (int i = 0; i < f_manager.getFeatureCount(); i++)
        para_Feature[i][0] = dep(i);
    if (ESTIMATE_TD)
        para_Td[0][0] = td;
}

void Estimator::double2vector()
{
    Vector3d origin_R0 = Utility::R2ypr(Rs[0]);
    Vector3d origin_P0 = Ps[0];

    if (failure_occur)
    {
        origin_R0 = Utility::R2ypr(last_R0);
        origin_P0 = last_P0;
        failure_occur = 0;
    }
    Vector3d origin_R00 = Utility::R2ypr(Quaterniond(para_Pose[0][6],
                                                      para_Pose[0][3],
                                                      para_Pose[0][4],
                                                      para_Pose[0][5]).toRotationMatrix());
    double y_diff = origin_R0.x() - origin_R00.x();
    //TODO
    Matrix3d rot_diff = Utility::ypr2R(Vector3d(y_diff, 0, 0));
    if (abs(abs(origin_R0.y()) - 90) < 1.0 || abs(abs(origin_R00.y()) - 90) < 1.0)
    {
        ROS_DEBUG("euler singular point!");
        rot_diff = Rs[0] * Quaterniond(para_Pose[0][6],
                                       para_Pose[0][3],
                                       para_Pose[0][4],
                                       para_Pose[0][5]).toRotationMatrix().transpose();
    }

    for (int i = 0; i <= WINDOW_SIZE; i++)
    {

        Rs[i] = rot_diff * Quaterniond(para_Pose[i][6], para_Pose[i][3], para_Pose[i][4], para_Pose[i][5]).normalized().toRotationMatrix();
        
        Ps[i] = rot_diff * Vector3d(para_Pose[i][0] - para_Pose[0][0],
                                para_Pose[i][1] - para_Pose[0][1],
                                para_Pose[i][2] - para_Pose[0][2]) + origin_P0;

        Vs[i] = rot_diff * Vector3d(para_SpeedBias[i][0],
                                    para_SpeedBias[i][1],
                                    para_SpeedBias[i][2]);

        Bas[i] = Vector3d(para_SpeedBias[i][3],
                          para_SpeedBias[i][4],
                          para_SpeedBias[i][5]);

        Bgs[i] = Vector3d(para_SpeedBias[i][6],
                          para_SpeedBias[i][7],
                          para_SpeedBias[i][8]);
    }

    for (int i = 0; i < NUM_OF_CAM; i++)
    {
        tic[i] = Vector3d(para_Ex_Pose[i][0],
                          para_Ex_Pose[i][1],
                          para_Ex_Pose[i][2]);
        ric[i] = Quaterniond(para_Ex_Pose[i][6],
                             para_Ex_Pose[i][3],
                             para_Ex_Pose[i][4],
                             para_Ex_Pose[i][5]).toRotationMatrix();
    }

    VectorXd dep = f_manager.getDepthVector();
    for (int i = 0; i < f_manager.getFeatureCount(); i++)
        dep(i) = para_Feature[i][0];
    f_manager.setDepth(dep);
    if (ESTIMATE_TD)
        td = para_Td[0][0];

    // relative info between two loop frame
    if(relocalization_info)
    { 
        Matrix3d relo_r;
        Vector3d relo_t;
        relo_r = rot_diff * Quaterniond(relo_Pose[6], relo_Pose[3], relo_Pose[4], relo_Pose[5]).normalized().toRotationMatrix();
        relo_t = rot_diff * Vector3d(relo_Pose[0] - para_Pose[0][0],
                                     relo_Pose[1] - para_Pose[0][1],
                                     relo_Pose[2] - para_Pose[0][2]) + origin_P0;
        double drift_correct_yaw;
        drift_correct_yaw = Utility::R2ypr(prev_relo_r).x() - Utility::R2ypr(relo_r).x();
        drift_correct_r = Utility::ypr2R(Vector3d(drift_correct_yaw, 0, 0));
        drift_correct_t = prev_relo_t - drift_correct_r * relo_t;   
        relo_relative_t = relo_r.transpose() * (Ps[relo_frame_local_index] - relo_t);
        relo_relative_q = relo_r.transpose() * Rs[relo_frame_local_index];
        relo_relative_yaw = Utility::normalizeAngle(Utility::R2ypr(Rs[relo_frame_local_index]).x() - Utility::R2ypr(relo_r).x());
        //cout << "vins relo " << endl;
        //cout << "vins relative_t " << relo_relative_t.transpose() << endl;
        //cout << "vins relative_yaw " <<relo_relative_yaw << endl;
        relocalization_info = 0;    

    }
}

bool Estimator::failureDetection()
{
    if (f_manager.last_track_num < 2) // ������ �����ӿ��� feature ���� 2�� �̸��� ���
    {
        ROS_INFO(" little feature %d", f_manager.last_track_num);
        //return true;
    }
    if (Bas[WINDOW_SIZE].norm() > 2.5) // acc bias�� norm�� 2.5���� ũ�ų�
    {
        ROS_INFO(" big IMU acc bias estimation %f", Bas[WINDOW_SIZE].norm());
        return true; // failure ���� ����
    }
    if (Bgs[WINDOW_SIZE].norm() > 1.0) // gyro bias�� norm�� 1.0���� ū ���
    {
        ROS_INFO(" big IMU gyr bias estimation %f", Bgs[WINDOW_SIZE].norm());
        return true; // failure ���� ����
    }
    /*
    if (tic(0) > 1)
    {
        ROS_INFO(" big extri param estimation %d", tic(0) > 1);
        return true;
    }
    */
    Vector3d tmp_P = Ps[WINDOW_SIZE]; // WINDOW_SIZE�� �ش��ϴ� camera pose ����
    if ((tmp_P - last_P).norm() > 5) // ���� camera pose���� ������ norm�� 5���� ũ��
    {
        ROS_INFO(" big translation");
        return true;  // failure ���� ����
    }
    if (abs(tmp_P.z() - last_P.z()) > 1) // ���� camera pose���� z�� ���̰� 1���� ũ��
    {
        ROS_INFO(" big z translation");
        return true; // failure ���� ����
    }
    Matrix3d tmp_R = Rs[WINDOW_SIZE]; // WINDOW_SIZE�� �ش��ϴ� tranlation ����� ������
    Matrix3d delta_R = tmp_R.transpose() * last_R; // ���� rotation ��İ� �� ���� ����
    Quaterniond delta_Q(delta_R); // ���ʹϾ����� ��ȯ
    double delta_angle;
    delta_angle = acos(delta_Q.w()) * 2.0 / 3.14 * 180.0; // delta_Q�� ���� angle ���
    if (delta_angle > 50) // ���� 50���� ũ��
    {
        ROS_INFO(" big delta_angle ");
        //return true;
    }
    return false; // failure ���� ����
}


void Estimator::optimization()
{
    ceres::Problem problem; // Optimization�� ���� Ceres Solver�� Problem ��ü�� ����
    ceres::LossFunction *loss_function; 
    //loss_function = new ceres::HuberLoss(1.0);
    loss_function = new ceres::CauchyLoss(1.0); // ����ȭ ����ġ ����
    for (int i = 0; i < WINDOW_SIZE + 1; i++) // WINDOW_SIZE��ŭ loop
    {
        ceres::LocalParameterization *local_parameterization = new PoseLocalParameterization(); // ����ȭ�� ����� �Ķ���� ����
        problem.AddParameterBlock(para_Pose[i], SIZE_POSE, local_parameterization); // ī�޶� pose�� Optimizatio ��ü�� ����
        problem.AddParameterBlock(para_SpeedBias[i], SIZE_SPEEDBIAS); // bias�� Optimizatio ��ü�� ����
    }
    for (int i = 0; i < NUM_OF_CAM; i++) // ī�޶� �� ��ŭ loop
    {
        ceres::LocalParameterization *local_parameterization = new PoseLocalParameterization(); // ����ȭ�� ����� �Ķ���� ����
        problem.AddParameterBlock(para_Ex_Pose[i], SIZE_POSE, local_parameterization); // Imu ���� ����� Optimizatio ��ü�� ����
        if (!ESTIMATE_EXTRINSIC) // ESTIMATE_EXTRINSIC�� 0�� ��� (ī�޶� IMU���� ����� �������� �ʴ� ���)
        {
            ROS_DEBUG("fix extinsic param");
            problem.SetParameterBlockConstant(para_Ex_Pose[i]); // Imu ���� ����� Optimizatio ��ü�� ���� (�� �������� Tic, Ric�� �Һ��ϰ� �ȴ�)
        }
        else
            ROS_DEBUG("estimate extinsic param");
    }
    if (ESTIMATE_TD) // Imu�� Camera �� time stamp�� ��ġ���� �ʴ� ���
    {
        problem.AddParameterBlock(para_Td[0], 1); // config.yaml���� ������ td ���� Optimizatio ��ü�� ����
        //problem.SetParameterBlockConstant(para_Td[0]);
    }

    TicToc t_whole, t_prepare;
    vector2double(); // ī�޶� pose, Ric, Tic�� para_Pose, para_Ex_Pose �迭�� ����

    if (last_marginalization_info) // ���� marginalization�� �����Ѵٸ�
    {
        // construct new marginlization_factor
        MarginalizationFactor *marginalization_factor = new MarginalizationFactor(last_marginalization_info); // ���ο� marginalization ��ü ���� (�̶� ���� ���� ���)
        problem.AddResidualBlock(marginalization_factor, NULL,
                                 last_marginalization_parameter_blocks); // Optimizatio ��ü�� ����
    }

    for (int i = 0; i < WINDOW_SIZE; i++) // WINDOW_SIZE��ŭ loop
    {
        int j = i + 1;
        if (pre_integrations[j]->sum_dt > 10.0) // dt�� 10���� ū ��� ����
            continue;
        IMUFactor* imu_factor = new IMUFactor(pre_integrations[j]); // Integration ������ ���� IMUFactor ���� (pre_integration ������ ������ ��ü)
        problem.AddResidualBlock(imu_factor, NULL, para_Pose[i], para_SpeedBias[i], para_Pose[j], para_SpeedBias[j]); // Optimizatio ��ü�� ����
    }
    int f_m_cnt = 0;
    int feature_index = -1;
    for (auto &it_per_id : f_manager.feature) // feature �迭 loop
    {
        it_per_id.used_num = it_per_id.feature_per_frame.size(); // feature�� ��µ� ����� frame ���� feature_per_frame �迭�� ũ��� ����
        if (!(it_per_id.used_num >= 2 && it_per_id.start_frame < WINDOW_SIZE - 2)) // feature�� ��µ� ���� frame ���� 2���� �۰ų� startFrame�� 8 �̻��̸� ����
            continue;
 
        ++feature_index; // feature index ����

        int imu_i = it_per_id.start_frame, imu_j = imu_i - 1; // imu_i�� ���� feature�� start_frame ��ȣ���ϰ� imu_j�� start_frame�� ���� index�� ��
        
        Vector3d pts_i = it_per_id.feature_per_frame[0].point; // feature_per_frame�� ���� 0��° feature�� ��ġ�� ������

        for (auto &it_per_frame : it_per_id.feature_per_frame) // feature_per_frame�迭 ��ȸ
        {
            imu_j++; // imu_j index ����
            if (imu_i == imu_j) // �� index�� ���� ��� ����
            {
                continue;
            }
            Vector3d pts_j = it_per_frame.point; // feature�� ��ġ�� ������
            if (ESTIMATE_TD) // Imu�� Camera �� time stamp�� ��ġ���� �ʴ� ���
            {
                    ProjectionTdFactor *f_td = new ProjectionTdFactor(pts_i, pts_j, it_per_id.feature_per_frame[0].velocity, it_per_frame.velocity,
                                                                     it_per_id.feature_per_frame[0].cur_td, it_per_frame.cur_td,
                                                                     it_per_id.feature_per_frame[0].uv.y(), it_per_frame.uv.y()); // feature��ġ�� velocity, td�� ���� Projection ��ü ����
                    problem.AddResidualBlock(f_td, loss_function, para_Pose[imu_i], para_Pose[imu_j], para_Ex_Pose[0], para_Feature[feature_index], para_Td[0]); // Optimizatio ��ü�� ����
                    /*
                    double **para = new double *[5];
                    para[0] = para_Pose[imu_i];
                    para[1] = para_Pose[imu_j];
                    para[2] = para_Ex_Pose[0];
                    para[3] = para_Feature[feature_index];
                    para[4] = para_Td[0];
                    f_td->check(para);
                    */
            }
            else
            {
                ProjectionFactor *f = new ProjectionFactor(pts_i, pts_j); // feature��ġ�� ���� Projection ��ü ����
                problem.AddResidualBlock(f, loss_function, para_Pose[imu_i], para_Pose[imu_j], para_Ex_Pose[0], para_Feature[feature_index]); // Optimizatio ��ü�� ����
            }
            f_m_cnt++;
        }
    }

    ROS_DEBUG("visual measurement count: %d", f_m_cnt);
    ROS_DEBUG("prepare for ceres: %f", t_prepare.toc());

    if(relocalization_info) // relocalization_info�� 0�� �ƴ� ���
    {
        //printf("set relocalization factor! \n");
        ceres::LocalParameterization *local_parameterization = new PoseLocalParameterization(); // Pose Local �Ķ���͸� ����
        problem.AddParameterBlock(relo_Pose, SIZE_POSE, local_parameterization); // Optimizatio ��ü�� ����
        int retrive_feature_index = 0;
        int feature_index = -1;
        for (auto &it_per_id : f_manager.feature) // feature ��� loop
        {
            it_per_id.used_num = it_per_id.feature_per_frame.size(); // feature�� ��µ� ����� frame ���� feature_per_frame �迭�� ũ��� ����
            if (!(it_per_id.used_num >= 2 && it_per_id.start_frame < WINDOW_SIZE - 2)) // feature�� ��µ� ���� frame ���� 2���� �۰ų� startFrame�� 8 �̻��̸� ����
                continue;
            ++feature_index;
            int start = it_per_id.start_frame; // ���� feature�� start_frame index ����
            if(start <= relo_frame_local_index) // start_frame index�� relocalization frame�� index���� �۰ų� ���� ���
            {   
                while((int)match_points[retrive_feature_index].z() < it_per_id.feature_id) // ��Ī�� �迭�� z ���� feature_id���� ������
                {
                    retrive_feature_index++; // ���� index ��ȣ��
                }
                if((int)match_points[retrive_feature_index].z() == it_per_id.feature_id) //// ��Ī�� �迭�� z ���� feature_id�� ���ٸ�
                {
                    Vector3d pts_j = Vector3d(match_points[retrive_feature_index].x(), match_points[retrive_feature_index].y(), 1.0); // ��Ī���� x, y���� ���� pts_j ����
                    Vector3d pts_i = it_per_id.feature_per_frame[0].point; // ���� feature�� ù ��° �����ӿ����� point ������ �̿��Ͽ� pts_i ����
                    
                    ProjectionFactor *f = new ProjectionFactor(pts_i, pts_j); // pts_i�� pts_j�� ���� Projection �Ķ���� ����
                    problem.AddResidualBlock(f, loss_function, para_Pose[start], relo_Pose, para_Ex_Pose[0], para_Feature[feature_index]); // Optimizatio ��ü�� ����
                    retrive_feature_index++; // ���� index ��ȣ��
                }     
            }
        }

    }

    ceres::Solver::Options options; // Optimization�� Option ��ü ����

    options.linear_solver_type = ceres::DENSE_SCHUR; // linear_solver_type�� Dense_schur�� ����
    //options.num_threads = 2;
    options.trust_region_strategy_type = ceres::DOGLEG;
    options.max_num_iterations = NUM_ITERATIONS; //  �ִ� �ݺ� Ƚ���� ���� (8ȸ)
    //options.use_explicit_schur_complement = true;
    //options.minimizer_progress_to_stdout = true;
    //options.use_nonmonotonic_steps = true;
    if (marginalization_flag == MARGIN_OLD) // marginalization_flag�� MARGIN_OLD�� ��� 
        options.max_solver_time_in_seconds = SOLVER_TIME * 4.0 / 5.0; // solver ���� �ð��� SOLVER_TIME(0.04) * 4/5 �� ����
    else
        options.max_solver_time_in_seconds = SOLVER_TIME; // solver ���� �ð��� SOLVER_TIME(0.04)�� ����
    TicToc t_solver; // ���� ���� �ð� ����
    ceres::Solver::Summary summary;
    ceres::Solve(options, &problem, &summary); // Optimization ������ Ǯ��, ����� summary�� ����
    //cout << summary.BriefReport() << endl;
    ROS_DEBUG("Iterations : %d", static_cast<int>(summary.iterations.size()));
    ROS_DEBUG("solver costs: %f", t_solver.toc());

    double2vector(); // para_Pose, para_Ex_Pose �迭�� �ִ� ���� ī�޶� pose, Ric, Tic�� ��ȯ

    TicToc t_whole_marginalization; // ���� ���� �ð� ����
    if (marginalization_flag == MARGIN_OLD) // marginalization_flag�� MARGIN_OLD�� ��� 
    {
        MarginalizationInfo *marginalization_info = new MarginalizationInfo(); // marginalizationInfo ��ü ����
        vector2double(); // ī�޶� pose, Ric, Tic�� para_Pose, para_Ex_Pose �迭�� ����

        if (last_marginalization_info) // last_marginalization_info�� �����ϴ� ���
        {
            vector<int> drop_set;
            for (int i = 0; i < static_cast<int>(last_marginalization_parameter_blocks.size()); i++) // last_marginalization_parameter_block �迭 ũ�⸸ŭ loop
            {
                if (last_marginalization_parameter_blocks[i] == para_Pose[0] ||
                    last_marginalization_parameter_blocks[i] == para_SpeedBias[0]) // last_marginalization_parameter_block�� ī�޶� pose�� ���ų� bias�� ���ٸ�
                    drop_set.push_back(i); // ���� index�� �迭�� ����
            }
            // construct new marginlization_factor
            MarginalizationFactor *marginalization_factor = new MarginalizationFactor(last_marginalization_info); // last_marginlization_info�� ���� MarginalizationFactor ��ü ����
            ResidualBlockInfo *residual_block_info = new ResidualBlockInfo(marginalization_factor, NULL,
                                                                           last_marginalization_parameter_blocks,
                                                                           drop_set); // Factor�� ���� ResidualBlockInfo ��ü ����

            marginalization_info->addResidualBlockInfo(residual_block_info); // marginalization_info ��ü�� ResidualBlockInfo ��ü�� ����
        }

        {
            if (pre_integrations[1]->sum_dt < 10.0) // pre_integrations �迭�� ù ��° ����� dt�� 10 �̸��� ���
            {
                IMUFactor* imu_factor = new IMUFactor(pre_integrations[1]); // ù ��° ��Ҹ� ���� Factor ��ü ����
                ResidualBlockInfo *residual_block_info = new ResidualBlockInfo(imu_factor, NULL,
                                                                           vector<double *>{para_Pose[0], para_SpeedBias[0], para_Pose[1], para_SpeedBias[1]},
                                                                           vector<int>{0, 1}); // Factor�� ���� ResidualBlockInfo ��ü ����
                marginalization_info->addResidualBlockInfo(residual_block_info); // marginalization_info ��ü�� ResidualBlockInfo ��ü�� ����
            }
        }

        {
            int feature_index = -1;
            for (auto &it_per_id : f_manager.feature) // feature �迭 ��ȸ
            {
                it_per_id.used_num = it_per_id.feature_per_frame.size(); // feature�� ��µ� ����� frame ���� feature_per_frame �迭�� ũ��� ����
                if (!(it_per_id.used_num >= 2 && it_per_id.start_frame < WINDOW_SIZE - 2)) // feature�� ��µ� ���� frame ���� 2���� �۰ų� startFrame�� 8 �̻��̸� ����
                    continue;

                ++feature_index; // index ����

                int imu_i = it_per_id.start_frame, imu_j = imu_i - 1; // imu_i�� ���� feature�� start_frame ��ȣ���ϰ� imu_j�� start_frame�� ���� index�� ��
                if (imu_i != 0) // start_frame�� 0�� �ƴ� ���
                    continue; // ����

                Vector3d pts_i = it_per_id.feature_per_frame[0].point; // feature_per_frame�� 0��° ����� point�� ������

                for (auto &it_per_frame : it_per_id.feature_per_frame) // feature_per_frame �迭 ��ȸ
                {
                    imu_j++; 
                    if (imu_i == imu_j) // imu_i�� imu_j�� ���� ��� ����
                        continue;

                    Vector3d pts_j = it_per_frame.point; // feature_per_frame �迭 ����� point�� ������
                    if (ESTIMATE_TD) // Imu�� Camera �� time stamp�� ��ġ���� �ʴ� ���
                    {
                        ProjectionTdFactor *f_td = new ProjectionTdFactor(pts_i, pts_j, it_per_id.feature_per_frame[0].velocity, it_per_frame.velocity,
                                                                          it_per_id.feature_per_frame[0].cur_td, it_per_frame.cur_td,
                                                                          it_per_id.feature_per_frame[0].uv.y(), it_per_frame.uv.y()); // feature��ġ�� velocity, td�� ���� Projection ��ü ����
                        ResidualBlockInfo *residual_block_info = new ResidualBlockInfo(f_td, loss_function,
                                                                                        vector<double *>{para_Pose[imu_i], para_Pose[imu_j], para_Ex_Pose[0], para_Feature[feature_index], para_Td[0]},
                                                                                        vector<int>{0, 3}); // Factor, ī�޶� pose�� Ric, Tic, Feature ������ ���� ResidualBlockInfo ��ü ����
                        marginalization_info->addResidualBlockInfo(residual_block_info); // marginalization_info ��ü�� ResidualBlockInfo ��ü ����
                    }
                    else
                    {
                        ProjectionFactor *f = new ProjectionFactor(pts_i, pts_j); // feature��ġ�� ���� Projection ��ü ����
                        ResidualBlockInfo *residual_block_info = new ResidualBlockInfo(f, loss_function,
                                                                                       vector<double *>{para_Pose[imu_i], para_Pose[imu_j], para_Ex_Pose[0], para_Feature[feature_index]},
                                                                                       vector<int>{0, 3}); // Factor, ī�޶� pose�� Ric, Tic, Feature ������ ���� ResidualBlockInfo ��ü ����
                        marginalization_info->addResidualBlockInfo(residual_block_info); // marginalization_info ��ü�� ResidualBlockInfo ��ü ����
                    }
                }
            }
        }

        TicToc t_pre_margin; // ���� �ð� ����
        marginalization_info->preMarginalize(); // marginalization_info�� ���� preMarginalization ����
        ROS_DEBUG("pre marginalization %f ms", t_pre_margin.toc());
        
        TicToc t_margin;
        marginalization_info->marginalize(); // marginalization_info�� ���� marginalization ����
        ROS_DEBUG("marginalization %f ms", t_margin.toc());

        std::unordered_map<long, double *> addr_shift;
        for (int i = 1; i <= WINDOW_SIZE; i++) // WINDOW_SIZE��ŭ �ݺ�
        {
            addr_shift[reinterpret_cast<long>(para_Pose[i])] = para_Pose[i - 1]; // camera pose�� bias�� shift�� ����
            addr_shift[reinterpret_cast<long>(para_SpeedBias[i])] = para_SpeedBias[i - 1];
        }
        for (int i = 0; i < NUM_OF_CAM; i++) // ī�޶� �� ��ŭ loop
            addr_shift[reinterpret_cast<long>(para_Ex_Pose[i])] = para_Ex_Pose[i];  // Ric Tic�� shift�� ����
        if (ESTIMATE_TD) // Imu�� Camera �� time stamp�� ��ġ���� �ʴ� ���
        {
            addr_shift[reinterpret_cast<long>(para_Td[0])] = para_Td[0]; // td �� shift�� ����
        }
        vector<double *> parameter_blocks = marginalization_info->getParameterBlocks(addr_shift); // ������ shift�� ���� marginalization_info���� parameter_blocks�� ������

        if (last_marginalization_info) // last_marginlization_info�� �����ϴ� ���
            delete last_marginalization_info; // ��ü ����
        last_marginalization_info = marginalization_info; // last_marginlization_info ����
        last_marginalization_parameter_blocks = parameter_blocks; //last_marginalization_parameter_blocks ����
        
    }
    else
    {
        // last_marginalization_info�� �����ϰ�, last_marginalization_parameter_blocks�� para_Pose�� WINDOW_SIZE - 1��° ��Ұ� ���ԵǴ� ���
        if (last_marginalization_info &&
            std::count(std::begin(last_marginalization_parameter_blocks), std::end(last_marginalization_parameter_blocks), para_Pose[WINDOW_SIZE - 1]))
        {

            MarginalizationInfo *marginalization_info = new MarginalizationInfo(); // marginalizationInfo ��ü ����
            vector2double(); // ī�޶� pose, Ric, Tic�� para_Pose, para_Ex_Pose �迭�� ����
            if (last_marginalization_info) // last_marginalization_info�� �����ϴ� ���
            {
                vector<int> drop_set; 
                for (int i = 0; i < static_cast<int>(last_marginalization_parameter_blocks.size()); i++) // last_marginalization_parameter_block �迭 ũ�⸸ŭ loop
                {
                    ROS_ASSERT(last_marginalization_parameter_blocks[i] != para_SpeedBias[WINDOW_SIZE - 1]); // �� �迭�� ���� ������ ���α׷� ����
                    if (last_marginalization_parameter_blocks[i] == para_Pose[WINDOW_SIZE - 1]) // camera pose�� ������
                        drop_set.push_back(i); // �ش� index ����
                }
                // construct new marginlization_factor
                MarginalizationFactor *marginalization_factor = new MarginalizationFactor(last_marginalization_info);  // last_marginlization_info�� ���� MarginalizationFactor ��ü ����
                ResidualBlockInfo *residual_block_info = new ResidualBlockInfo(marginalization_factor, NULL,
                                                                               last_marginalization_parameter_blocks,
                                                                               drop_set);  // Factor�� ���� ResidualBlockInfo ��ü ����

                marginalization_info->addResidualBlockInfo(residual_block_info); // marginalization_info ��ü�� ResidualBlockInfo ��ü�� ����
            }

            TicToc t_pre_margin; // ���� �ð� ����
            ROS_DEBUG("begin marginalization");
            marginalization_info->preMarginalize();  // marginalization_info�� ���� preMarginalization ����
            ROS_DEBUG("end pre marginalization, %f ms", t_pre_margin.toc());
             
            TicToc t_margin; // ���� �ð� ����
            ROS_DEBUG("begin marginalization");
            marginalization_info->marginalize(); // marginalization_info�� ���� marginalization ����
            ROS_DEBUG("end marginalization, %f ms", t_margin.toc());
            
            std::unordered_map<long, double *> addr_shift;
            for (int i = 0; i <= WINDOW_SIZE; i++) // WINDOW_SIZE��ŭ �ݺ�
            {
                if (i == WINDOW_SIZE - 1) // i�� WINDOW_SIZE - 1(9)�� ��� ����
                    continue;
                else if (i == WINDOW_SIZE) // // i�� WINDOW_SIZE(10)�� ���
                {
                    addr_shift[reinterpret_cast<long>(para_Pose[i])] = para_Pose[i - 1];  // camera pose�� bias�� shift�� ���� (i�� �� pose�� key�� �ϰ�, i-1�� �� pose�� value��)
                    addr_shift[reinterpret_cast<long>(para_SpeedBias[i])] = para_SpeedBias[i - 1];
                }
                else // �Ϲ����� ���
                {
                    addr_shift[reinterpret_cast<long>(para_Pose[i])] = para_Pose[i];  // camera pose�� bias�� shift�� ���� (i�� �� pose�� key�� �ϰ�, i�� �� pose�� value��)
                    addr_shift[reinterpret_cast<long>(para_SpeedBias[i])] = para_SpeedBias[i];
                }
            }
            for (int i = 0; i < NUM_OF_CAM; i++)
                addr_shift[reinterpret_cast<long>(para_Ex_Pose[i])] = para_Ex_Pose[i]; // Ric Tic�� shift�� ����
            if (ESTIMATE_TD)
            {
                addr_shift[reinterpret_cast<long>(para_Td[0])] = para_Td[0];  // td �� shift�� ����
            }
            
            vector<double *> parameter_blocks = marginalization_info->getParameterBlocks(addr_shift); // ������ shift�� ���� marginalization_info���� parameter_blocks�� ������
            if (last_marginalization_info) // last_marginlization_info�� �����ϴ� ���
                delete last_marginalization_info; // ��ü ����
            last_marginalization_info = marginalization_info; // last_marginlization_info ����
            last_marginalization_parameter_blocks = parameter_blocks; //last_marginalization_parameter_blocks ����
             
        }
    }
    ROS_DEBUG("whole marginalization costs: %f", t_whole_marginalization.toc());
    
    ROS_DEBUG("whole time for ceres: %f", t_whole.toc()); // �� �ҿ� �ð� ���
}

void Estimator::slideWindow()
{
    TicToc t_margin; // ���� �ð� ����
    if (marginalization_flag == MARGIN_OLD) // marginlization_flag�� MARGIN_OLD�� ���
    {
        double t_0 = Headers[0].stamp.toSec(); // time stamp�� t_0�� ����
        back_R0 = Rs[0]; // 0��° ī�޶� rotation ��� ����
        back_P0 = Ps[0]; // 0��° ī�޶� pose ����
        if (frame_count == WINDOW_SIZE) // frame_count�� WINDOW_SIZE�� ���ٸ�
        {
            for (int i = 0; i < WINDOW_SIZE; i++) // WINDOW_SIZE ��ŭ ��ȸ
            {
                Rs[i].swap(Rs[i + 1]); // Rotation �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�

                std::swap(pre_integrations[i], pre_integrations[i + 1]); // integration �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�

                dt_buf[i].swap(dt_buf[i + 1]); // dt ������ i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                linear_acceleration_buf[i].swap(linear_acceleration_buf[i + 1]); // ���ӵ� �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                angular_velocity_buf[i].swap(angular_velocity_buf[i + 1]); // gyro �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�

                Headers[i] = Headers[i + 1]; // Headers �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                Ps[i].swap(Ps[i + 1]); // ī�޶� pose �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                Vs[i].swap(Vs[i + 1]); // �ӵ� �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                Bas[i].swap(Bas[i + 1]); // ���ӵ� bias �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
                Bgs[i].swap(Bgs[i + 1]); // ���̷� bias �迭�� i��° ��ҿ� i + 1��° ����� ������ �ٲ�
            }
            Headers[WINDOW_SIZE] = Headers[WINDOW_SIZE - 1]; // WINDOW_SIZE ��° ��Ҹ� WINDOW_SIZE - 1 ��° ��ҷ� ����
            Ps[WINDOW_SIZE] = Ps[WINDOW_SIZE - 1];
            Vs[WINDOW_SIZE] = Vs[WINDOW_SIZE - 1];
            Rs[WINDOW_SIZE] = Rs[WINDOW_SIZE - 1];
            Bas[WINDOW_SIZE] = Bas[WINDOW_SIZE - 1];
            Bgs[WINDOW_SIZE] = Bgs[WINDOW_SIZE - 1];

            delete pre_integrations[WINDOW_SIZE];  // WINDOW_SIZE ��° ��ü ����
            pre_integrations[WINDOW_SIZE] = new IntegrationBase{acc_0, gyr_0, Bas[WINDOW_SIZE], Bgs[WINDOW_SIZE]}; // WINDOW_SIZE ��° ��ü�� ���Ӱ� �����ؼ� ����

            dt_buf[WINDOW_SIZE].clear(); // ���� �ʱ�ȭ
            linear_acceleration_buf[WINDOW_SIZE].clear();
            angular_velocity_buf[WINDOW_SIZE].clear();

            if (true || solver_flag == INITIAL) // solver_flag�� INITIAL�� ��� (������ true�� �־� �׻� ����ϴ� if��)
            {
                map<double, ImageFrame>::iterator it_0; // <doble, ImageFrame> ������ map�� ����Ű�� iterator ����
                it_0 = all_image_frame.find(t_0); // ��� �̹��� frame �迭���� t_0�� key�� �ϴ� value�� ã�´�
                delete it_0->second.pre_integration; // pre_integration ��ü�� ������
                it_0->second.pre_integration = nullptr; // null�� ������ �Ѵ�.
 
                for (map<double, ImageFrame>::iterator it = all_image_frame.begin(); it != it_0; ++it) // ��� �̹��� ������ �迭 ��ȸ
                {
                    if (it->second.pre_integration) // pre_integration ��ü�� �ִٸ�
                        delete it->second.pre_integration; // ��ü ����
                    it->second.pre_integration = NULL; // null�� �������Ѵ�.
                }

                all_image_frame.erase(all_image_frame.begin(), it_0); // ��� �̹��� ������ �迭���� it_0���� �ִ� ��Ҹ� ������
                all_image_frame.erase(t_0); // t_0�� key�� �ϴ� value�� ����

            }
            slideWindowOld(); // slide window�� ���� ������ ������ �� ���¸� ����
        }
    }
    else
    {
        if (frame_count == WINDOW_SIZE) // frame_count�� WINDOW_SIZE�� ���ٸ�
        {
            for (unsigned int i = 0; i < dt_buf[frame_count].size(); i++)
            {
                double tmp_dt = dt_buf[frame_count][i]; // dt ���ۿ��� ���� �������� dt�� ������
                Vector3d tmp_linear_acceleration = linear_acceleration_buf[frame_count][i]; // ���ӵ� ���ۿ��� ���� �������� ���ӵ��� ������
                Vector3d tmp_angular_velocity = angular_velocity_buf[frame_count][i]; // ���̷� ���ۿ��� ���� �������� ���̷θ� ������

                pre_integrations[frame_count - 1]->push_back(tmp_dt, tmp_linear_acceleration, tmp_angular_velocity); // ���� ������ ���� ������ ����

                dt_buf[frame_count - 1].push_back(tmp_dt);
                linear_acceleration_buf[frame_count - 1].push_back(tmp_linear_acceleration);
                angular_velocity_buf[frame_count - 1].push_back(tmp_angular_velocity);
            }

            Headers[frame_count - 1] = Headers[frame_count]; // ���� Header ���� ���� Header ������ ����
            Ps[frame_count - 1] = Ps[frame_count]; // ī�޶� ��ġ, �ӵ�, ȸ��, bias�� ����
            Vs[frame_count - 1] = Vs[frame_count];
            Rs[frame_count - 1] = Rs[frame_count];
            Bas[frame_count - 1] = Bas[frame_count];
            Bgs[frame_count - 1] = Bgs[frame_count];

            delete pre_integrations[WINDOW_SIZE];
            pre_integrations[WINDOW_SIZE] = new IntegrationBase{acc_0, gyr_0, Bas[WINDOW_SIZE], Bgs[WINDOW_SIZE]}; // WINDOW_SIZE ��° pre_integrations ��ü ����

            dt_buf[WINDOW_SIZE].clear(); // WINDOW_SIZE��° ���� ����
            linear_acceleration_buf[WINDOW_SIZE].clear();
            angular_velocity_buf[WINDOW_SIZE].clear();

            slideWindowNew(); // ���ο� ������ �� ���¸� ����
        }
    }
}

// real marginalization is removed in solve_ceres()
void Estimator::slideWindowNew()
{
    sum_of_front++; // sum_of_back 1 ����
    f_manager.removeFront(frame_count); // feature �迭 ����
}
// real marginalization is removed in solve_ceres()
void Estimator::slideWindowOld()
{
    sum_of_back++; // sum_of_back 1 ����

    bool shift_depth = solver_flag == NON_LINEAR ? true : false; // solver_flag ���� NON_LINEAR�� ��� shift_depth�� true�� ����
    if (shift_depth) // shift_depth�� true�� ���
    {
        Matrix3d R0, R1;
        Vector3d P0, P1;
        R0 = back_R0 * ric[0]; // ���� ī�޶� rotation ���U�� Ric�� ����
        R1 = Rs[0] * ric[0]; // 0��° ī�޶� rotation ��Ŀ� Ric�� ����
        P0 = back_P0 + back_R0 * tic[0]; // ���� ī�޶� rotation ��İ� Tic�� ���ϰ� ī�޶� pose�� ����
        P1 = Ps[0] + Rs[0] * tic[0]; // 0��° ī�޶� rotation ��İ� Tic�� ���ϰ� 0��° ī�޶� pose�� ����
        f_manager.removeBackShiftDepth(R0, P0, R1, P1); // feature �迭�� depth�� ������ �� feature �迭 ����
    }
    else
        f_manager.removeBack(); // feature �迭 ����
}

void Estimator::setReloFrame(double _frame_stamp, int _frame_index, vector<Vector3d> &_match_points, Vector3d _relo_t, Matrix3d _relo_r)
{
    relo_frame_stamp = _frame_stamp; // relocalization�� time stamp ����
    relo_frame_index = _frame_index; // relocalization�� frame index ����
    match_points.clear(); // ��Ī �� ��� �ʱ�ȭ
    match_points = _match_points; // ��Ī�� ����� ���ڷ� ���� ������
    prev_relo_t = _relo_t; // ��ȯ ���
    prev_relo_r = _relo_r; // ȸ�� ���
    for(int i = 0; i < WINDOW_SIZE; i++)
    {
        if(relo_frame_stamp == Headers[i].stamp.toSec()) // relocalization�� frame�� time stamp�� ���ݱ��� ������ frame �� �ϳ���� 
        {
            relo_frame_local_index = i; // ������ frame�� index�� ����
            relocalization_info = 1; // relocalization�� �� �ֵ��� frag ����
            for (int j = 0; j < SIZE_POSE; j++)
                relo_Pose[j] = para_Pose[i][j]; // relocalization�� frame�� index�� �ش��ϴ� ��ġ�� ȸ�� ��� ���� ����
        }
    }
}


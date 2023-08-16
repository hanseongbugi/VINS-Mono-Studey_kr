#include "feature_tracker.h"

int FeatureTracker::n_id = 0;

bool inBorder(const cv::Point2f &pt)
{
    const int BORDER_SIZE = 1; // �̹��� ��迡�� ���Ǵ� ������ ũ�� ����
    int img_x = cvRound(pt.x); // ���ڷ� ���� ��ǥ�� ������ �ݿø�
    int img_y = cvRound(pt.y);
    return BORDER_SIZE <= img_x && img_x < COL - BORDER_SIZE && BORDER_SIZE <= img_y && img_y < ROW - BORDER_SIZE; // point�� �̹��� ��� ���� �ִ��� Ȯ��
}

void reduceVector(vector<cv::Point2f> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++) // ���ڷ� ���� �迭�� ��ȸ
        if (status[i]) // status�� true�� ���
            v[j++] = v[i]; // ���� ��Ҹ� v[j]��ġ�� ���� (��, true�� ���鸸 �迭�� ��� �ȴ�)
    v.resize(j); // �迭 ũ�� ����
}

void reduceVector(vector<int> &v, vector<uchar> status)
{
    int j = 0;
    for (int i = 0; i < int(v.size()); i++) // �������̵� �Ǿ ���� �Լ�
        if (status[i])
            v[j++] = v[i];
    v.resize(j);
}


FeatureTracker::FeatureTracker()
{
}

void FeatureTracker::setMask()
{
    if(FISHEYE) // FishEye ī�޶��� ���
        mask = fisheye_mask.clone(); // mask�� fisheye ����ũ�� ��
    else
        mask = cv::Mat(ROW, COL, CV_8UC1, cv::Scalar(255)); // frame�� row�� col�� �°� mask�� ����
    

    // prefer to keep features that are tracked for long time
    vector<pair<int, pair<cv::Point2f, int>>> cnt_pts_id;

    for (unsigned int i = 0; i < forw_pts.size(); i++)
        cnt_pts_id.push_back(make_pair(track_cnt[i], make_pair(forw_pts[i], ids[i]))); // �ش� Ư¡���� ������ Ƚ��, �ȼ� ��ǥ, ������ Ư¡���� �ε����� ����

    sort(cnt_pts_id.begin(), cnt_pts_id.end(), [](const pair<int, pair<cv::Point2f, int>> &a, const pair<int, pair<cv::Point2f, int>> &b)
         {
            return a.first > b.first; // ���� Ƚ���� ������� ���� �������� �迭�� ����
         });

    forw_pts.clear(); // �迭 �ʱ�ȭ
    ids.clear();
    track_cnt.clear();

    for (auto &it : cnt_pts_id) // ������ �迭�� ��ȸ
    {
        if (mask.at<uchar>(it.second.first) == 255) // feature point�� mask�� ���� ������ �� 255�� ��� 
        {
            forw_pts.push_back(it.second.first); // point�� �迭�� ����
            ids.push_back(it.second.second); // index�� �迭�� ����
            track_cnt.push_back(it.first); // ���� Ƚ���� �迭�� ����
            cv::circle(mask, it.second.first, MIN_DIST, 0, -1); // �̹� ���õ� point�� mask���� �ش� ������ 0���� �����Ͽ� �ߺ� ������ ����
        }
    }
}

void FeatureTracker::addPoints()
{
    for (auto &p : n_pts) // ���Ӱ� ���� feature �迭�� ��ȸ
    {
        forw_pts.push_back(p); // feature point �迭�� feature�� ����
        ids.push_back(-1); // feature index�� -1�� �ʱ�ȭ
        track_cnt.push_back(1); // ���� count�� 1�� �ʱ�ȭ
    }
}

void FeatureTracker::readImage(const cv::Mat &_img, double _cur_time)
{
    cv::Mat img; // frame�� ���� Mat ��ü ����
    TicToc t_r; // ���� ���� �ð� ����
    cur_time = _cur_time; // ���ڷ� ���� time stamp ����

    if (EQUALIZE) // config ���Ͽ��� EQUAIZE�� 1�� �� ���
    {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(3.0, cv::Size(8, 8)); // Equalization�� ���� CLAHE(Contrast Limited Adaptive Histogram Equalization)��ü ����
        TicToc t_c; // ���� �ð� ����
        clahe->apply(_img, img); // img�� Equalization�� frame ����
        ROS_DEBUG("CLAHE costs: %fms", t_c.toc());
    }
    else
        img = _img; // ���ڷ� ���� frame ����

    if (forw_img.empty()) // forw_img�� ��� �ִٸ�
    {
        prev_img = cur_img = forw_img = img; // ���� �̹���, ���� �̹���, forward img�� ���� �̹����� ���� (ó�� �̹����� ����)
    }
    else
    {
        forw_img = img; // forward img�� ���� �̹����� ����
    }

    forw_pts.clear(); 

    if (cur_pts.size() > 0) // ���� Ư¡�� ��ġ �迭�� ���� �ִ� ���
    {
        TicToc t_o; // ����ð� ����
        vector<uchar> status;
        vector<float> err;
        //���� frame, forward frame, ���� frame���� Ư¡�� ��ġ, forward �����ӿ��� Ư¡�� ��ġ, Ư¡�� ������ ������ ��� 1�� �����Ǵ� �迭, ���� ���� ���, ������ ũ��, �Ƕ�̵� ũ�⸦
        // ���� ��Ƽ�� �÷ο츦 ���� (���� �̹����� ���� �̹��� �� Ư¡������ �������� ��ô)
        cv::calcOpticalFlowPyrLK(cur_img, forw_img, cur_pts, forw_pts, status, err, cv::Size(21, 21), 3); 

        for (int i = 0; i < int(forw_pts.size()); i++) // forward frame Ư¡�� �迭 ��ȸ
            if (status[i] && !inBorder(forw_pts[i])) // Ư¡�� ������ �����Ͽ���, point�� �̹��� ��� ���� �ִٸ�
                status[i] = 0; // ���� ���¸� 0���� ����
        reduceVector(prev_pts, status); // status�� true�� ���鸸 ����� �迭 ũ�� ����
        reduceVector(cur_pts, status);
        reduceVector(forw_pts, status);
        reduceVector(ids, status);
        reduceVector(cur_un_pts, status);
        reduceVector(track_cnt, status);
        ROS_DEBUG("temporal optical flow costs: %fms", t_o.toc());
    }

    for (auto &n : track_cnt) // tracking �迭�� ��ȸ�ϸ� count�� ����
        n++;

    if (PUB_THIS_FRAME) // Publish�� Frame�� ���
    {
        rejectWithF(); // Fundamental Matrix�� ����Ͽ� ���� Ư¡������ ���͸�
        ROS_DEBUG("set mask begins");
        TicToc t_m; // ���� ���� �ð� ����
        setMask(); // ������ ����� ������ feature�� ����
        ROS_DEBUG("set mask costs %fms", t_m.toc());

        ROS_DEBUG("detect feature begins");
        TicToc t_t; 
        int n_max_cnt = MAX_CNT - static_cast<int>(forw_pts.size()); // MAX_CNT(150)�� feature point�� ������ ���̸� ����
        if (n_max_cnt > 0) // feature point�� ���� �ִ� ���� �������� �ʾҴٸ�
        {
            if(mask.empty())
                cout << "mask is empty " << endl;
            if (mask.type() != CV_8UC1)
                cout << "mask type wrong " << endl;
            if (mask.size() != forw_img.size())
                cout << "wrong size " << endl;
            cv::goodFeaturesToTrack(forw_img, n_pts, MAX_CNT - forw_pts.size(), 0.01, MIN_DIST, mask); // ���� �̹������� ���ο� feature�� ����, �����Ǵ� Ư¡���� ������ MAX_CNT - forw_pts.size()�� ����
        }
        else
            n_pts.clear(); // �ִ�ġ�� ��� ������ ���� ���ο� feature�� ������.
        ROS_DEBUG("detect feature costs: %fms", t_t.toc());

        ROS_DEBUG("add feature begins");
        TicToc t_a;
        addPoints(); // ���ο� frame���� ���ο� feature���� ���� feature��� �Բ� forw_pts, ids, track_cnt�� �߰��Ǿ� ���� ���� �� ������ ����
        ROS_DEBUG("selectFeature costs: %fms", t_a.toc());
    }
    prev_img = cur_img; // ���� frame�� ���� frame����
    prev_pts = cur_pts; // ���� point �迭�� ���� point �迭��
    prev_un_pts = cur_un_pts; // ���� undistorted �迭�� ���� �迭��
    cur_img = forw_img; // ���� frame�� forward frame����
    cur_pts = forw_pts; // ���� point �迭�� forward point �迭��
    undistortedPoints();
    prev_time = cur_time;
}

void FeatureTracker::rejectWithF()
{
    if (forw_pts.size() >= 8) // forward feature point�� 8�� �̻� �����ϴ� ���
    {
        ROS_DEBUG("FM ransac begins");
        TicToc t_f; // ���� �ǻ� �ð� ����
        vector<cv::Point2f> un_cur_pts(cur_pts.size()), un_forw_pts(forw_pts.size()); // feature point�� ���� �迭�� ũ�⸸ŭ �迭 ����
        for (unsigned int i = 0; i < cur_pts.size(); i++) // ���� feature point �迭 ũ�⸸ŭ �ݺ�
        {
            Eigen::Vector3d tmp_p;
            m_camera->liftProjective(Eigen::Vector2d(cur_pts[i].x, cur_pts[i].y), tmp_p); // camera calibration��ü�� ���� ���� feature point�� Reprojection
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + COL / 2.0; // ������� �� ������ x, y ��ǥ�� ���� undistorted point ����
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + ROW / 2.0;
            un_cur_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());

            m_camera->liftProjective(Eigen::Vector2d(forw_pts[i].x, forw_pts[i].y), tmp_p);
            tmp_p.x() = FOCAL_LENGTH * tmp_p.x() / tmp_p.z() + COL / 2.0;
            tmp_p.y() = FOCAL_LENGTH * tmp_p.y() / tmp_p.z() + ROW / 2.0;
            un_forw_pts[i] = cv::Point2f(tmp_p.x(), tmp_p.y());
        }

        vector<uchar> status;
        cv::findFundamentalMat(un_cur_pts, un_forw_pts, cv::FM_RANSAC, F_THRESHOLD, 0.99, status); // undistorted�� ���� feature��� forward feature���� RANSAC�� ���� Funamental Matrix ���
        int size_a = cur_pts.size();
        reduceVector(prev_pts, status);  // status�� true�� �� (Fundamental Matrix�� �����ϴ� feature) �鸸 ����� �迭 ũ�� ����
        reduceVector(cur_pts, status);
        reduceVector(forw_pts, status);
        reduceVector(cur_un_pts, status);
        reduceVector(ids, status);
        reduceVector(track_cnt, status);
        ROS_DEBUG("FM ransac: %d -> %lu: %f", size_a, forw_pts.size(), 1.0 * forw_pts.size() / size_a);
        ROS_DEBUG("FM ransac costs: %fms", t_f.toc());
    }
}

bool FeatureTracker::updateID(unsigned int i)
{
    if (i < ids.size()) 
    {
        if (ids[i] == -1)
            ids[i] = n_id++;
        return true;
    }
    else
        return false;
}

void FeatureTracker::readIntrinsicParameter(const string &calib_file)
{
    ROS_INFO("reading paramerter of camera %s", calib_file.c_str()); // config���� �̸��� ���
    m_camera = CameraFactory::instance()->generateCameraFromYamlFile(calib_file); //CameraFactory ��ü�� ������ config���Ͽ� �ִ� ī�޶� model_type(�ַ� pinhole)�� �´� �÷��׸� ����
}

void FeatureTracker::showUndistortion(const string &name)
{
    cv::Mat undistortedImg(ROW + 600, COL + 600, CV_8UC1, cv::Scalar(0));
    vector<Eigen::Vector2d> distortedp, undistortedp;
    for (int i = 0; i < COL; i++)
        for (int j = 0; j < ROW; j++)
        {
            Eigen::Vector2d a(i, j);
            Eigen::Vector3d b;
            m_camera->liftProjective(a, b);
            distortedp.push_back(a);
            undistortedp.push_back(Eigen::Vector2d(b.x() / b.z(), b.y() / b.z()));
            //printf("%f,%f->%f,%f,%f\n)\n", a.x(), a.y(), b.x(), b.y(), b.z());
        }
    for (int i = 0; i < int(undistortedp.size()); i++)
    {
        cv::Mat pp(3, 1, CV_32FC1);
        pp.at<float>(0, 0) = undistortedp[i].x() * FOCAL_LENGTH + COL / 2;
        pp.at<float>(1, 0) = undistortedp[i].y() * FOCAL_LENGTH + ROW / 2;
        pp.at<float>(2, 0) = 1.0;
        //cout << trackerData[0].K << endl;
        //printf("%lf %lf\n", p.at<float>(1, 0), p.at<float>(0, 0));
        //printf("%lf %lf\n", pp.at<float>(1, 0), pp.at<float>(0, 0));
        if (pp.at<float>(1, 0) + 300 >= 0 && pp.at<float>(1, 0) + 300 < ROW + 600 && pp.at<float>(0, 0) + 300 >= 0 && pp.at<float>(0, 0) + 300 < COL + 600)
        {
            undistortedImg.at<uchar>(pp.at<float>(1, 0) + 300, pp.at<float>(0, 0) + 300) = cur_img.at<uchar>(distortedp[i].y(), distortedp[i].x());
        }
        else
        {
            //ROS_ERROR("(%f %f) -> (%f %f)", distortedp[i].y, distortedp[i].x, pp.at<float>(1, 0), pp.at<float>(0, 0));
        }
    }
    cv::imshow(name, undistortedImg);
    cv::waitKey(0);
}

void FeatureTracker::undistortedPoints()
{
    cur_un_pts.clear(); // undistorted point�迭�� �ʱ�ȭ
    cur_un_pts_map.clear();
    //cv::undistortPoints(cur_pts, un_pts, K, cv::Mat());
    for (unsigned int i = 0; i < cur_pts.size(); i++) // ���� feature point �迭�� ��ȸ
    {
        Eigen::Vector2d a(cur_pts[i].x, cur_pts[i].y); // feature�� point�� ����
        Eigen::Vector3d b;
        m_camera->liftProjective(a, b); // Reprojection�� ����
        cur_un_pts.push_back(cv::Point2f(b.x() / b.z(), b.y() / b.z())); // �����ǥ�� �ٽ� ��ȯ�Ͽ� ��ǥ ����
        cur_un_pts_map.insert(make_pair(ids[i], cv::Point2f(b.x() / b.z(), b.y() / b.z())));
        //printf("cur pts id %d %f %f", ids[i], cur_un_pts[i].x, cur_un_pts[i].y);
    }
    // caculate points velocity
    if (!prev_un_pts_map.empty()) // ���� frame������ feature point�� ���Ͽ��ٸ�
    {
        double dt = cur_time - prev_time; // dt�� ���� frame�� ���� frame�� �ð� ���̷� ����
        pts_velocity.clear();
        for (unsigned int i = 0; i < cur_un_pts.size(); i++)  // Ư¡�� point �迭 ��ȸ
        {
            if (ids[i] != -1) // index�� -1�� �ƴ� ���
            {
                std::map<int, cv::Point2f>::iterator it;
                it = prev_un_pts_map.find(ids[i]);
                if (it != prev_un_pts_map.end())  // ���� feature point map���� ids[i]�� �ش��ϴ� point�� �����Ѵٸ�
                {
                    double v_x = (cur_un_pts[i].x - it->second.x) / dt; // feature point�� �ð����̸� ���� �ӵ� ���
                    double v_y = (cur_un_pts[i].y - it->second.y) / dt;
                    pts_velocity.push_back(cv::Point2f(v_x, v_y)); // �ӵ� ����
                }
                else
                    pts_velocity.push_back(cv::Point2f(0, 0)); // ������ �ӵ� 0
            }
            else
            {
                pts_velocity.push_back(cv::Point2f(0, 0)); // index�� -1�� ��� �ӵ� 0
            }
        }
    }
    else
    {
        for (unsigned int i = 0; i < cur_pts.size(); i++)
        {
            pts_velocity.push_back(cv::Point2f(0, 0)); // // ���� frame������ feature point�� ������ ���ϸ� �ӵ� 0
        }
    }
    prev_un_pts_map = cur_un_pts_map; // ���� feature point�� ���� feature point�� ����
}

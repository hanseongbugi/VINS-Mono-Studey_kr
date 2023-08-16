#include "initial_ex_rotation.h"

InitialEXRotation::InitialEXRotation(){
    frame_count = 0;
    Rc.push_back(Matrix3d::Identity());
    Rc_g.push_back(Matrix3d::Identity());
    Rimu.push_back(Matrix3d::Identity());
    ric = Matrix3d::Identity();
}

bool InitialEXRotation::CalibrationExRotation(vector<pair<Vector3d, Vector3d>> corres, Quaterniond delta_q_imu, Matrix3d &calib_ric_result)
{
    frame_count++; // frame count ����
    Rc.push_back(solveRelativeR(corres)); // correspondent�� ���� Relative Roation ��� ����
    Rimu.push_back(delta_q_imu.toRotationMatrix()); // ���ʹϾ��� imu ȸ�� ����� Matrix3d ��ü�� ��ȯ�� �迭�� ����
    Rc_g.push_back(ric.inverse() * delta_q_imu * ric); // ric(���� ���� ���)�� ����Ŀ� delta_q(imu ȸ�� ���)�� ric�� ���� ȸ�� ��� ���� �� �迭�� ����

    Eigen::MatrixXd A(frame_count * 4, 4); //ũ�Ⱑ frame_count * 4x4 ������ ��� A�� ����
    A.setZero();
    int sum_ok = 0;
    for (int i = 1; i <= frame_count; i++) //frame_count ��ŭ �ݺ�
    {
        Quaterniond r1(Rc[i]); // i��° Relative Roation ����� ���ʹϾ����� ��ȯ
        Quaterniond r2(Rc_g[i]); // i��° ������ ȸ�� ����� ���ʹϾ����� ��ȯ

        double angular_distance = 180 / M_PI * r1.angularDistance(r2); //ȸ�� ��İ� ������ ȸ�� ��� ������ ���� ���̸� ���
        ROS_DEBUG(
            "%d %f", i, angular_distance);

        double huber = angular_distance > 5.0 ? 5.0 / angular_distance : 1.0; //���� ���̰� 5.0���� ũ�� huber�� 1�� ����, �׷��� ������ 5.0/���� ���̷� ����
        ++sum_ok;
        Matrix4d L, R; //ũ�Ⱑ 4x4�� �� ���� ��� L�� R�� ����

        double w = Quaterniond(Rc[i]).w(); // Relative Roation ����� ���ʹϾ����� ��ȯ �� w���� ������ (x, y, z�� ���� ��, w�� ��Į��(roll) ��)
        Vector3d q = Quaterniond(Rc[i]).vec(); //Relative Roation ����� ���ʹϾ����� ��ȯ �� x,y, z���� ������
        L.block<3, 3>(0, 0) = w * Matrix3d::Identity() + Utility::skewSymmetric(q); // L ����� 3x3 ���� ȸ�� �� ����
        L.block<3, 1>(0, 3) = q; // L ����� 3x1 ���� translation �� ����
        L.block<1, 3>(3, 0) = -q.transpose(); // L ����� 1x3 ���� ȸ�� ����� ��ġ ��� ����
        L(3, 3) = w; // ȸ�� ����� w �� ����

        Quaterniond R_ij(Rimu[i]); // imu ȸ�� ����� ���ʹϾ����� ��ȯ
        w = R_ij.w(); //w���� ������
        q = R_ij.vec(); //x,y, z���� ������
        R.block<3, 3>(0, 0) = w * Matrix3d::Identity() - Utility::skewSymmetric(q); // R ����� 3x3 ���� ȸ�� �� ����
        R.block<3, 1>(0, 3) = q; // R ����� 3x1 ���� translation �� ����
        R.block<1, 3>(3, 0) = -q.transpose(); // R ����� 1x3 ���� ȸ�� ����� ��ġ ��� ����
        R(3, 3) = w; // ȸ�� ����� w �� ����

        A.block<4, 4>((i - 1) * 4, 0) = huber * (L - R); //ȸ�� ��� L�� R�� ���̿� huber �� ��ŭ ���ؼ� A ��Ŀ� ����
    }

    JacobiSVD<MatrixXd> svd(A, ComputeFullU | ComputeFullV); //A ����� ����
    Matrix<double, 4, 1> x = svd.matrixV().col(3); //ȸ�� ����� �������� ��Ÿ���� 4x1 ��� ����
    Quaterniond estimated_R(x); //ȸ�� ����� �������� ��Ÿ���� ���ʹϾ� ��ü ����
    ric = estimated_R.toRotationMatrix().inverse(); //���ʹϾ��� ȸ�� ��ķ� ��ȯ�� ��, �� ������� ���Ͽ� ric�� ����
    //cout << svd.singularValues().transpose() << endl;
    //cout << ric << endl;
    Vector3d ric_cov;
    ric_cov = svd.singularValues().tail<3>(); //ȸ�� ����� �������� �󸶳� ��Ȯ���� �� ������

    // ��������� ������ ���� WINDOW_SIZE(10) �̻��̰�, ������ ȸ�� ����� ������ �� �� ��° ���� 0.25���� ũ�ٸ�
    if (frame_count >= WINDOW_SIZE && ric_cov(1) > 0.25)
    {
        calib_ric_result = ric; // ������ RIC ����� calib_ric_result(���ڷ� ���� RIC ���)�� ����
        return true; // ���� ����
    }
    else
        return false; //���� ����
}

Matrix3d InitialEXRotation::solveRelativeR(const vector<pair<Vector3d, Vector3d>> &corres)
{
    if (corres.size() >= 9) // correspondent �迭�� ũ�Ⱑ 9 �̻��̸� (corespondent�� 9�� �̻��̸�)
    {
        vector<cv::Point2f> ll, rr; // correspondent�� ��ǥ�� ��� �迭
        for (int i = 0; i < int(corres.size()); i++)
        {
            ll.push_back(cv::Point2f(corres[i].first(0), corres[i].first(1))); //���� ��ǥ (x, y) ����
            rr.push_back(cv::Point2f(corres[i].second(0), corres[i].second(1))); // ������ ��ǥ (x, y) ����
        }
        cv::Mat E = cv::findFundamentalMat(ll, rr); //��ǥ�� ���� Fundamental ��� ���
        cv::Mat_<double> R1, R2, t1, t2;
        decomposeE(E, R1, R2, t1, t2); //Fundamental ����� �� ���� ȸ�� ��İ� �� ���� ��ȯ ���ͷ� ����

        if (determinant(R1) + 1.0 < 1e-09)  //ù ��° ȸ�� ��� R1�� ��Ľ��� ������ ���
        {
            E = -E; // Fundamental ����� ���� ��Ų��.
            decomposeE(E, R1, R2, t1, t2); ////������ Fundamental ����� �� ���� ȸ�� ��İ� �� ���� ��ȯ ��ķ� ����
        }
        //�� ���� ȸ�� ��� R1, R2�� �̿��Ͽ� triangulation�� ������ �� ratio�� ���
        double ratio1 = max(testTriangulation(ll, rr, R1, t1), testTriangulation(ll, rr, R1, t2));
        double ratio2 = max(testTriangulation(ll, rr, R2, t1), testTriangulation(ll, rr, R2, t2));
        cv::Mat_<double> ans_R_cv = ratio1 > ratio2 ? R1 : R2; //�� ȸ�� ��� �� ratio�� ���� ȸ�� ����� ����

        Matrix3d ans_R_eigen;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                ans_R_eigen(j, i) = ans_R_cv(i, j); //ans_R_cv�� �����͸� ans_R_eigen���� ����
        return ans_R_eigen; //relative rotation ��� ��ȯ
    }
    return Matrix3d::Identity();
}

double InitialEXRotation::testTriangulation(const vector<cv::Point2f> &l,
                                          const vector<cv::Point2f> &r,
                                          cv::Mat_<double> R, cv::Mat_<double> t)
{
    cv::Mat pointcloud; // triangulation�� ���� ������ ������ ������ ���
    cv::Matx34f P = cv::Matx34f(1, 0, 0, 0,
                                0, 1, 0, 0,
                                0, 0, 1, 0); //��� �����ϴ� ��ȯ��� �ʱ�ȭ
    cv::Matx34f P1 = cv::Matx34f(R(0, 0), R(0, 1), R(0, 2), t(0),
                                 R(1, 0), R(1, 1), R(1, 2), t(1),
                                 R(2, 0), R(2, 1), R(2, 2), t(2)); // ȸ�� ��İ� ��ȯ ����� ���� ��� ���� ��� ����
    cv::triangulatePoints(P, P1, l, r, pointcloud); // triangulation�� ���� �ΰ��� Ư¡�� �迭�� �����Ǵ� ������ ����, ������ ������ pointcloud�� ����
    int front_count = 0; // ������ ������ ����
    for (int i = 0; i < pointcloud.cols; i++) //������ ������ ��ȸ
    {
        double normal_factor = pointcloud.col(i).at<float>(3); // ���� ����κ��� normal factor�� ������

        cv::Mat_<double> p_3d_l = cv::Mat(P) * (pointcloud.col(i) / normal_factor); //ù ��° ���� ����� �̿��Ͽ� ������ ��� ����
        cv::Mat_<double> p_3d_r = cv::Mat(P1) * (pointcloud.col(i) / normal_factor); //�� ��° ���� ����� �̿��Ͽ� ������ ��� ����
        if (p_3d_l(2) > 0 && p_3d_r(2) > 0) //���� frame�� ���ʿ� ��ġ�� �ִ� ��� (z ���� ����� ���)
            front_count++; //front_count ����
    }
    ROS_DEBUG("MotionEstimator: %f", 1.0 * front_count / pointcloud.cols);
    return 1.0 * front_count / pointcloud.cols; //ratio ����ϰ� ��ȯ
}

void InitialEXRotation::decomposeE(cv::Mat E,
                                 cv::Mat_<double> &R1, cv::Mat_<double> &R2,
                                 cv::Mat_<double> &t1, cv::Mat_<double> &t2)
{
    cv::SVD svd(E, cv::SVD::MODIFY_A); //Fundamental ����� SVD�� ���� ���� (E = u * s * vt�� ����� ����)
    cv::Matx33d W(0, -1, 0,
                  1, 0, 0,
                  0, 0, 1); // ȸ�� ����� �����ϱ� ���� W�� Wt�� ���� (W�� Z���� �������� 90�� ȸ����Ű�� ���, Wt�� W�� �����)
    cv::Matx33d Wt(0, 1, 0,
                   -1, 0, 0,
                   0, 0, 1);
    R1 = svd.u * cv::Mat(W) * svd.vt; //ù ��° ȸ�� ��� R1�� ���
    R2 = svd.u * cv::Mat(Wt) * svd.vt; //�� ��° ȸ�� ��� R2�� ���
    t1 = svd.u.col(2); //ù ��° ��ȯ ��� t1�� ���
    t2 = -svd.u.col(2); //�� ��° ��ȯ ��� t2�� ���
}

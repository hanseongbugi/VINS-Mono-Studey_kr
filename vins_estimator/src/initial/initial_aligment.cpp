#include "initial_alignment.h"

void solveGyroscopeBias(map<double, ImageFrame> &all_image_frame, Vector3d* Bgs)
{
    Matrix3d A; // Bias ������ ���Ǵ� ��� ����
    Vector3d b;
    Vector3d delta_bg; // gyro bias�� ���� ��
    A.setZero(); // A�� b ����� 0��ķ� �ʱ�ȭ
    b.setZero();
    map<double, ImageFrame>::iterator frame_i; // all_image_frame�� �����ϱ� ���� iterator ����
    map<double, ImageFrame>::iterator frame_j;
    for (frame_i = all_image_frame.begin(); next(frame_i) != all_image_frame.end(); frame_i++) // ��� �̹�ġ frame�� ���� ��ȸ
    {
        frame_j = next(frame_i); // frame_i�� ����Ű�� frame�� ���� frame�� ������
        MatrixXd tmp_A(3, 3); // 3x3 ��� ����
        tmp_A.setZero(); // 0���� �ʱ�ȭ
        VectorXd tmp_b(3); // 3x1 ��� ����
        tmp_b.setZero(); // 0���� �ʱ�ȭ
        Eigen::Quaterniond q_ij(frame_i->second.R.transpose() * frame_j->second.R); // �� �������� Rotation ����� ���� �� ������ ���� Rotation�� ��Ÿ���� ���ʹϾ��� ���
        tmp_A = frame_j->second.pre_integration->jacobian.template block<3, 3>(O_R, O_BG); // pre_integration�� jacobian ����� �Ϻ� ���
        tmp_b = 2 * (frame_j->second.pre_integration->delta_q.inverse() * q_ij).vec(); //imu ȸ�� ��İ� �����ΰ� Rotation�� ���� tmp_b ����
        A += tmp_A.transpose() * tmp_A; // tmp_A�� ��ġ�� tmp_A�� ���� A ��Ŀ� ����
        b += tmp_A.transpose() * tmp_b; // tmp_A�� ��ġ�� tmp_b�� ���� B ��Ŀ� ����

    }
    delta_bg = A.ldlt().solve(b); // A * delta_bg = b ��� �������� Ǯ��(LDLT ����) bias ���� �� ����
    ROS_WARN_STREAM("gyroscope bias initial calibration " << delta_bg.transpose());

    for (int i = 0; i <= WINDOW_SIZE; i++) // WINDOWũ�⸸ŭ ��ȸ
        Bgs[i] += delta_bg; // ���̾ ���� Bgs �迭�� ������Ʈ

    for (frame_i = all_image_frame.begin(); next(frame_i) != all_image_frame.end( ); frame_i++)
    {
        frame_j = next(frame_i);
        frame_j->second.pre_integration->repropagate(Vector3d::Zero(), Bgs[0]); // bias�� ���� IMU �����͸� �纸��
    }
}


MatrixXd TangentBasis(Vector3d &g0)
{
    Vector3d b, c;
    Vector3d a = g0.normalized(); // �߷� ���͸� normalization�Ͽ� a ���ͷ� ��
    Vector3d tmp(0, 0, 1);
    if(a == tmp) // a ���Ϳ� tmp ���Ͱ� ���ٸ� 
        tmp << 1, 0, 0; //tmp ���͸� (1, 0, 0)�� ����
    b = (tmp - a * (a.transpose() * tmp)).normalized(); // a���Ϳ� tmp�� ���� b ���� ����
    c = a.cross(b); // a�� b ������ ������ ���� c ���� ����
    MatrixXd bc(3, 2); // b�� c ���͸� ���� ���� 3x2 ũ���� ��� bc�� ����
    bc.block<3, 1>(0, 0) = b;
    bc.block<3, 1>(0, 1) = c; 
    return bc; // bc ��� ��ȯ
}

void RefineGravity(map<double, ImageFrame> &all_image_frame, Vector3d &g, VectorXd &x)
{
    Vector3d g0 = g.normalized() * G.norm(); // �߷� ���͸� normalization�ϰ� G������ ũ��� ���� �� g0�� ����
    Vector3d lx, ly;
    //VectorXd x;
    int all_frame_count = all_image_frame.size(); // ��� �̹��� ��
    int n_state = all_frame_count * 3 + 2 + 1; // state�� ũ�� ����

    MatrixXd A{n_state, n_state}; // A����� NxN���� ����
    A.setZero();
    VectorXd b{n_state}; // B ����� Nx1�� ����
    b.setZero();

    map<double, ImageFrame>::iterator frame_i; // all_image_frame�� �����ϱ� ���� iterator ����
    map<double, ImageFrame>::iterator frame_j;
    for(int k = 0; k < 4; k++)
    {
        MatrixXd lxly(3, 2);
        lxly = TangentBasis(g0); // �߷� ���͸� ���� ����(basis) ����
        int i = 0;
        for (frame_i = all_image_frame.begin(); next(frame_i) != all_image_frame.end(); frame_i++, i++) // ��� �̹�ġ frame�� ���� ��ȸ
        {
            frame_j = next(frame_i); // frame_i�� ����Ű�� frame�� ���� frame�� ������

            MatrixXd tmp_A(6, 9); // tmp_A �� tmp_b ��� ����
            tmp_A.setZero();
            VectorXd tmp_b(6);
            tmp_b.setZero();

            double dt = frame_j->second.pre_integration->sum_dt; // frame dt�� ������


            tmp_A.block<3, 3>(0, 0) = -dt * Matrix3d::Identity(); // imu�� Rotation �� Translation, TIC ���� ���� tmp_A ��� ����
            tmp_A.block<3, 2>(0, 6) = frame_i->second.R.transpose() * dt * dt / 2 * Matrix3d::Identity() * lxly; // Rotation ��Ŀ� �߷� ���� ������ ����
            tmp_A.block<3, 1>(0, 8) = frame_i->second.R.transpose() * (frame_j->second.T - frame_i->second.T) / 100.0;     
            tmp_b.block<3, 1>(0, 0) = frame_j->second.pre_integration->delta_p + frame_i->second.R.transpose() * frame_j->second.R * TIC[0] - TIC[0] - frame_i->second.R.transpose() * dt * dt / 2 * g0;

            tmp_A.block<3, 3>(3, 0) = -Matrix3d::Identity();
            tmp_A.block<3, 3>(3, 3) = frame_i->second.R.transpose() * frame_j->second.R;
            tmp_A.block<3, 2>(3, 6) = frame_i->second.R.transpose() * dt * Matrix3d::Identity() * lxly; // Rotation ��Ŀ� �߷� ���� ������ ����
            tmp_b.block<3, 1>(3, 0) = frame_j->second.pre_integration->delta_v - frame_i->second.R.transpose() * dt * Matrix3d::Identity() * g0;


            Matrix<double, 6, 6> cov_inv = Matrix<double, 6, 6>::Zero(); // Imu�� ����ġ ���
            //cov.block<6, 6>(0, 0) = IMU_cov[i + 1];
            //MatrixXd cov_inv = cov.inverse();
            cov_inv.setIdentity(); // ���� ��ķ� ����

            MatrixXd r_A = tmp_A.transpose() * cov_inv * tmp_A; // tmp_A����� ��ġ�ϰ� ����ġ�� tmp_A ����� ���Ͽ� r_A�� ��
            VectorXd r_b = tmp_A.transpose() * cov_inv * tmp_b; // tmp_A����� ��ġ�ϰ� ����ġ�� tmp_b ����� ���Ͽ� r_b�� ��

            A.block<6, 6>(i * 3, i * 3) += r_A.topLeftCorner<6, 6>(); // r_A�� A ��Ŀ� ����
            b.segment<6>(i * 3) += r_b.head<6>(); // r_b�� b ��Ŀ� ����

            A.bottomRightCorner<3, 3>() += r_A.bottomRightCorner<3, 3>();
            b.tail<3>() += r_b.tail<3>();

            A.block<6, 3>(i * 3, n_state - 3) += r_A.topRightCorner<6, 3>();
            A.block<3, 6>(n_state - 3, i * 3) += r_A.bottomLeftCorner<3, 6>();
        }
            A = A * 1000.0; // A �� b�� 1000�� ����
            b = b * 1000.0;
            x = A.ldlt().solve(b); // A * x = b ��� �������� Ǭ��(LDLT ����)
            VectorXd dg = x.segment<2>(n_state - 3); // x�� ������ 2�� ��Ҹ� �����Ͽ� dg ���ͷ� ���� (�߷� ���� ��)
            g0 = (g0 + lxly * dg).normalized() * G.norm(); // g0�� ������ �������� ���Ͽ� normalization�ϰ� G������ ũ��� ���� �� g0�� ����
            //double s = x(n_state - 1);
    }   
    g = g0; // ������ g0�� ���ڷ� ���� g�� ����
}

bool LinearAlignment(map<double, ImageFrame> &all_image_frame, Vector3d &g, VectorXd &x)
{
    int all_frame_count = all_image_frame.size(); //��ü �̹��� �������� ��
    int n_state = all_frame_count * 3 + 3 + 1; // state�� ũ�� ����

    MatrixXd A{n_state, n_state}; // A����� NxN���� ����
    A.setZero(); // 0��ķ� �ʱ�ȭ
    VectorXd b{n_state}; // B ����� Nx1�� ����
    b.setZero(); // 0��ķ� �ʱ�ȭ

    map<double, ImageFrame>::iterator frame_i; // all_image_frame�� �����ϱ� ���� iterator ����
    map<double, ImageFrame>::iterator frame_j;
    int i = 0;
    for (frame_i = all_image_frame.begin(); next(frame_i) != all_image_frame.end(); frame_i++, i++) // ��� �̹�ġ frame�� ���� ��ȸ
    {
        frame_j = next(frame_i); // frame_i�� ����Ű�� frame�� ���� frame�� ������

        MatrixXd tmp_A(6, 10); // tmp_A �� tmp_b ��� ����
        tmp_A.setZero();
        VectorXd tmp_b(6);
        tmp_b.setZero();

        double dt = frame_j->second.pre_integration->sum_dt; // frame dt�� ������

        tmp_A.block<3, 3>(0, 0) = -dt * Matrix3d::Identity(); // imu�� Rotation �� Translation, TIC ���� ���� tmp_A ��� ����
        tmp_A.block<3, 3>(0, 6) = frame_i->second.R.transpose() * dt * dt / 2 * Matrix3d::Identity();
        tmp_A.block<3, 1>(0, 9) = frame_i->second.R.transpose() * (frame_j->second.T - frame_i->second.T) / 100.0;     
        tmp_b.block<3, 1>(0, 0) = frame_j->second.pre_integration->delta_p + frame_i->second.R.transpose() * frame_j->second.R * TIC[0] - TIC[0];
        //cout << "delta_p   " << frame_j->second.pre_integration->delta_p.transpose() << endl;
        tmp_A.block<3, 3>(3, 0) = -Matrix3d::Identity(); 
        tmp_A.block<3, 3>(3, 3) = frame_i->second.R.transpose() * frame_j->second.R;
        tmp_A.block<3, 3>(3, 6) = frame_i->second.R.transpose() * dt * Matrix3d::Identity();
        tmp_b.block<3, 1>(3, 0) = frame_j->second.pre_integration->delta_v;
        //cout << "delta_v   " << frame_j->second.pre_integration->delta_v.transpose() << endl;

        Matrix<double, 6, 6> cov_inv = Matrix<double, 6, 6>::Zero(); // Imu�� ����ġ ���
        //cov.block<6, 6>(0, 0) = IMU_cov[i + 1];
        //MatrixXd cov_inv = cov.inverse();
        cov_inv.setIdentity(); // ���� ��ķ� ����

        MatrixXd r_A = tmp_A.transpose() * cov_inv * tmp_A; // tmp_A����� ��ġ�ϰ� ����ġ�� tmp_A ����� ���Ͽ� r_A�� ��
        VectorXd r_b = tmp_A.transpose() * cov_inv * tmp_b; // tmp_A����� ��ġ�ϰ� ����ġ�� tmp_b ����� ���Ͽ� r_b�� ��

        A.block<6, 6>(i * 3, i * 3) += r_A.topLeftCorner<6, 6>(); // r_A�� A ��Ŀ� ����
        b.segment<6>(i * 3) += r_b.head<6>(); // r_b�� b ��Ŀ� ����

        A.bottomRightCorner<4, 4>() += r_A.bottomRightCorner<4, 4>();
        b.tail<4>() += r_b.tail<4>();

        A.block<6, 4>(i * 3, n_state - 4) += r_A.topRightCorner<6, 4>();
        A.block<4, 6>(n_state - 4, i * 3) += r_A.bottomLeftCorner<4, 6>();
    }
    A = A * 1000.0; // A �� b�� 1000�� ����
    b = b * 1000.0;
    x = A.ldlt().solve(b); // A * x = b ��� �������� Ǭ��(LDLT ����)
    double s = x(n_state - 1) / 100.0; // x���� Scale �� ����
    ROS_DEBUG("estimated scale: %f", s);
    g = x.segment<3>(n_state - 4); // x���� �߷� ���� g�� ����
    ROS_DEBUG_STREAM(" result g     " << g.norm() << " " << g.transpose());
    if(fabs(g.norm() - G.norm()) > 1.0 || s < 0) // ���� �� ���� �̹� ���ǵ� ��(9.8)�� ���̰� �ʹ� ũ�ų� ������ ���� ���� �������� 0 ���ϸ� ����
    {
        return false;
    }

    RefineGravity(all_image_frame, g, x); // �߷� ���͸� ����
    s = (x.tail<1>())(0) / 100.0; // ������ ������ ��Ҹ� ���� 
    (x.tail<1>())(0) = s; 
    ROS_DEBUG_STREAM(" refine     " << g.norm() << " " << g.transpose());
    if(s < 0.0 ) //�������� ������ ��� ����
        return false;   
    else
        return true;
}

bool VisualIMUAlignment(map<double, ImageFrame> &all_image_frame, Vector3d* Bgs, Vector3d &g, VectorXd &x)
{
    solveGyroscopeBias(all_image_frame, Bgs); //Imu�� gyro bias�� ����

    if(LinearAlignment(all_image_frame, g, x)) // �߷� ���� ����, ���������� ���� �Ǿ��ٸ�
        return true;
    else 
        return false;
}

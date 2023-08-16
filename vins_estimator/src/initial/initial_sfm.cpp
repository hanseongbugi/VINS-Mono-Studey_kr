#include "initial_sfm.h"

GlobalSFM::GlobalSFM(){}

void GlobalSFM::triangulatePoint(Eigen::Matrix<double, 3, 4> &Pose0, Eigen::Matrix<double, 3, 4> &Pose1,
						Vector2d &point0, Vector2d &point1, Vector3d &point_3d)
{
	Matrix4d design_matrix = Matrix4d::Zero(); // 4x4 0��� �ʱ�ȭ
	design_matrix.row(0) = point0[0] * Pose0.row(2) - Pose0.row(0); // ù��° ���� 1��° x �� * 1��° pose�� 3��° �� - 1��° ������
	design_matrix.row(1) = point0[1] * Pose0.row(2) - Pose0.row(1); // �ι�° ���� 1��° y �� * 1��° pose�� 3��° �� - 2��° ������
	design_matrix.row(2) = point1[0] * Pose1.row(2) - Pose1.row(0); // ����° ���� 2��° x �� * 2��° pose�� 3��° �� - 1��° ������
	design_matrix.row(3) = point1[1] * Pose1.row(2) - Pose1.row(1); // �׹�° ���� 2��° y �� * 1��° pose�� 3��° �� - 2��° ������
	Vector4d triangulated_point;
	triangulated_point =
		      design_matrix.jacobiSvd(Eigen::ComputeFullV).matrixV().rightCols<1>(); // design_matrix�� Singular Value Decomposition(Ư�̰� ����)�Ͽ� triangulated_point ���
	point_3d(0) = triangulated_point(0) / triangulated_point(3); // triangulated_point�� ������ element�� ���� ��������� x, y, z���� ����
	point_3d(1) = triangulated_point(1) / triangulated_point(3);
	point_3d(2) = triangulated_point(2) / triangulated_point(3);
}


bool GlobalSFM::solveFrameByPnP(Matrix3d &R_initial, Vector3d &P_initial, int i,
								vector<SFMFeature> &sfm_f)
{
	vector<cv::Point2f> pts_2_vector; // 2D point �迭
	vector<cv::Point3f> pts_3_vector; // 3D point �迭
	for (int j = 0; j < feature_num; j++) // Ư¡�� ���� ��ŭ �ݺ�
	{
		if (sfm_f[j].state != true) // j��° SFMFeature ��ü�� state�� true�� �ƴ� ��� (������ �Ұ����ϴٸ�)
			continue;
		Vector2d point2d; 
		for (int k = 0; k < (int)sfm_f[j].observation.size(); k++) // SFMFeature�� observation �迭�� ũ�⸸ŭ �ݺ�
		{
			if (sfm_f[j].observation[k].first == i) // observation �迭�� feature index�� i(PnP�� ������ frame ��ȣ)�� ���� ���
			{
				Vector2d img_pts = sfm_f[j].observation[k].second; // observation �迭���� Ư¡�� ��ǥ ��ȯ
				cv::Point2f pts_2(img_pts(0), img_pts(1)); 
				pts_2_vector.push_back(pts_2); // 2D Point �迭�� ����
				cv::Point3f pts_3(sfm_f[j].position[0], sfm_f[j].position[1], sfm_f[j].position[2]); 
				pts_3_vector.push_back(pts_3); // 3D Point �迭�� ����
				break;
			}
		}
	}
	if (int(pts_2_vector.size()) < 15) // 2D Point�� 15�� �̸��� ���
	{
		printf("unstable features tracking, please slowly move you device!\n");
		if (int(pts_2_vector.size()) < 10) // 2D Point�� 10�� �̸��� ���
			return false; // ����
	}
	cv::Mat r, rvec, t, D, tmp_r;
	cv::eigen2cv(R_initial, tmp_r); // ī�޶� Rotation ����� opencv�� Mat ��ü�� ��ȯ
	cv::Rodrigues(tmp_r, rvec); // PnP�� ���� ����� Rodrigues ��ȯ
	cv::eigen2cv(P_initial, t); // ī�޶� Translation ����� opencv�� Mat ��ü�� ��ȯ
	cv::Mat K = (cv::Mat_<double>(3, 3) << 1, 0, 0, 0, 1, 0, 0, 0, 1); // K ��� �ʱ�ȭ
	bool pnp_succ; 
	pnp_succ = cv::solvePnP(pts_3_vector, pts_2_vector, K, D, rvec, t, 1); //solvePnP�� ���� PnP ����
	if(!pnp_succ) // PnP�� ������ ���
	{
		return false; // ����
	}
	cv::Rodrigues(rvec, r); // Rodrigues ��ȯ�� ����� ���� ������ ��ķ� ��ȯ
	//cout << "r " << endl << r << endl;
	MatrixXd R_pnp; 
	cv::cv2eigen(r, R_pnp); // Roatation ����� Matrix3d ��ü�� ��ȯ
	MatrixXd T_pnp;
	cv::cv2eigen(t, T_pnp); // Translation ����� Matrix3d ��ü�� ��ȯ
	R_initial = R_pnp; // Rotation �� Translation ��� ����
	P_initial = T_pnp;
	return true; // ����

}

void GlobalSFM::triangulateTwoFrames(int frame0, Eigen::Matrix<double, 3, 4> &Pose0, 
									 int frame1, Eigen::Matrix<double, 3, 4> &Pose1,
									 vector<SFMFeature> &sfm_f)
{
	assert(frame0 != frame1); //frame0�� index�� frame1�� index�� ������ ����
	for (int j = 0; j < feature_num; j++) // Ư¡�� ������ŭ �ݺ�
	{
		if (sfm_f[j].state == true) // SFMFeature ��ü�� state�� true�� ���
			continue; // ����
		bool has_0 = false, has_1 = false;
		Vector2d point0;
		Vector2d point1;
		for (int k = 0; k < (int)sfm_f[j].observation.size(); k++) // SFMFeature ��ü�� observation �迭 ũ�⸸ŭ �ݺ�
		{
			if (sfm_f[j].observation[k].first == frame0) // observation �迭 �� feature�� index�� frame0 ���� ���ٸ�
			{
				point0 = sfm_f[j].observation[k].second; // feature point�� �����´�.
				has_0 = true; // feature�� �ִٰ� frag ����
			}
			if (sfm_f[j].observation[k].first == frame1) // observation �迭 �� feature�� index�� frame1 ���� ���ٸ�
			{
				point1 = sfm_f[j].observation[k].second; // feature point�� �����´�.
				has_1 = true; // feature�� �ִٰ� frag ����
			}
		}
		if (has_0 && has_1) // 2���� feature point�� �����Ѵٸ�
		{
			Vector3d point_3d;
			triangulatePoint(Pose0, Pose1, point0, point1, point_3d); // point�� ���� triangulation ����
			sfm_f[j].state = true; // state�� true�� ��ȯ
			sfm_f[j].position[0] = point_3d(0); // SFMFeature ��ü�� point�� triangulation�� ������ point�� ����
			sfm_f[j].position[1] = point_3d(1);
			sfm_f[j].position[2] = point_3d(2);
			//cout << "trangulated : " << frame1 << "  3d point : "  << j << "  " << point_3d.transpose() << endl;
		}							  
	}
}
// construct(frame_count + 1, Q, T, l,
// relative_R, relative_T,
// sfm_f, sfm_tracked_points
// 	 q w_R_cam t w_R_cam
//  c_rotation cam_R_w 
//  c_translation cam_R_w
// relative_q[i][j]  j_q_i
// relative_t[i][j]  j_t_ji  (j < i)
bool GlobalSFM::construct(int frame_num, Quaterniond* q, Vector3d* T, int l,
			  const Matrix3d relative_R, const Vector3d relative_T,
			  vector<SFMFeature> &sfm_f, map<int, Vector3d> &sfm_tracked_points)
{
	feature_num = sfm_f.size(); // SFMFeature ��ü�� ����ִ� �迭�� ũ��
	//cout << "set 0 and " << l << " as known " << endl;
	// have relative_r relative_t
	// intial two view
	q[l].w() = 1; // ��� frame �ε����� ī�޶� ȸ�� ����� w(��Į��) �� �ʱ�ȭ
	q[l].x() = 0; // ��� frame �ε����� ī�޶� ȸ�� ����� x, y,z �� �ʱ�ȭ
	q[l].y() = 0;
	q[l].z() = 0;
	T[l].setZero(); // ��� frame �ε����� ī�޶� ��ġ ����� 0��ķ� �ʱ�ȭ 
	q[frame_num - 1] = q[l] * Quaterniond(relative_R); // ���� �������� ī�޶� ȸ�� ��� = ��� frame�� ī�޶� ��� * ��� ȸ�� ��� 
	T[frame_num - 1] = relative_T; // ���� �������� ī�޶� ��ġ ��� = ��� frame�� ī�޶� ��� * ��� ȸ�� ��� 
	//cout << "init q_l " << q[l].w() << " " << q[l].vec().transpose() << endl;
	//cout << "init t_l " << T[l].transpose() << endl;

	//rotate to cam frame
	Matrix3d c_Rotation[frame_num]; 
	Vector3d c_Translation[frame_num];
	Quaterniond c_Quat[frame_num];
	double c_rotation[frame_num][4];
	double c_translation[frame_num][3];
	Eigen::Matrix<double, 3, 4> Pose[frame_num];

	c_Quat[l] = q[l].inverse(); // ī�޶� ȸ�� ����� ������ ���� 
	c_Rotation[l] = c_Quat[l].toRotationMatrix(); // ���ʹϾ��� ī�޶� ȸ�� ����� Matrix3d ��ü�� ��ȯ
	c_Translation[l] = -1 * (c_Rotation[l] * T[l]); // ȸ�� ��İ� ��ġ ����� ���� Translation ��� ����
	Pose[l].block<3, 3>(0, 0) = c_Rotation[l]; // Rotation ��İ� Translation ����� ���� l ��° ī�޶� pose ����
	Pose[l].block<3, 1>(0, 3) = c_Translation[l];

	c_Quat[frame_num - 1] = q[frame_num - 1].inverse(); // ���� �����ӿ� ���� ī�޶� pose ����
	c_Rotation[frame_num - 1] = c_Quat[frame_num - 1].toRotationMatrix();
	c_Translation[frame_num - 1] = -1 * (c_Rotation[frame_num - 1] * T[frame_num - 1]);
	Pose[frame_num - 1].block<3, 3>(0, 0) = c_Rotation[frame_num - 1];
	Pose[frame_num - 1].block<3, 1>(0, 3) = c_Translation[frame_num - 1];


	//1: trangulate between l ----- frame_num - 1
	//2: solve pnp l + 1; trangulate l + 1 ------- frame_num - 1; 
	for (int i = l; i < frame_num - 1 ; i++)
	{
		// solve pnp
		if (i > l) //l��° ������ ������ ���
		{
			Matrix3d R_initial = c_Rotation[i - 1];
			Vector3d P_initial = c_Translation[i - 1];
			if(!solveFrameByPnP(R_initial, P_initial, i, sfm_f)) // PnP�� ���� Rotation ��İ� Translation ����� ����
				return false; // PnP ���� �� false ��ȯ
			c_Rotation[i] = R_initial; // ī�޶� ����� ������ ������ ����
			c_Translation[i] = P_initial; 
			c_Quat[i] = c_Rotation[i];
			Pose[i].block<3, 3>(0, 0) = c_Rotation[i]; // pose �� ���� 
			Pose[i].block<3, 1>(0, 3) = c_Translation[i];
		}

		// triangulate point based on the solve pnp result
		triangulateTwoFrames(i, Pose[i], frame_num - 1, Pose[frame_num - 1], sfm_f); // PnP�� ���� ���� pose�� �̿��Ͽ� trianglate ���
	}
	//3: triangulate l-----l+1 l+2 ... frame_num -2
	for (int i = l + 1; i < frame_num - 1; i++) // l + 1��° frame�� frame_num -2 ���� point�� triangulate
		triangulateTwoFrames(l, Pose[l], i, Pose[i], sfm_f);
	//4: solve pnp l-1; triangulate l-1 ----- l
	//             l-2              l-2 ----- l
	for (int i = l - 1; i >= 0; i--) // l - 1 ���� 0���� �ݺ�
	{
		//solve pnp
		Matrix3d R_initial = c_Rotation[i + 1]; // ī�޶� Rotation ���
		Vector3d P_initial = c_Translation[i + 1]; // ī�޶� Translation ���
		if(!solveFrameByPnP(R_initial, P_initial, i, sfm_f)) // PnP�� ���� Rotation ��İ� Translation ����� ����
			return false;
		c_Rotation[i] = R_initial; // ī�޶� ����� ������ ������ ����
		c_Translation[i] = P_initial;
		c_Quat[i] = c_Rotation[i];
		Pose[i].block<3, 3>(0, 0) = c_Rotation[i]; // pose �� ���� 
		Pose[i].block<3, 1>(0, 3) = c_Translation[i];
		//triangulate
		triangulateTwoFrames(i, Pose[i], l, Pose[l], sfm_f); // PnP�� ���� ���� pose�� �̿��Ͽ� trianglate ���
	}
	//5: triangulate all other points
	for (int j = 0; j < feature_num; j++) // ��� feature�� ���� �ݺ�
	{
		if (sfm_f[j].state == true) // SFMFeature ��ü�� state�� true�� ��� ����
			continue;
		if ((int)sfm_f[j].observation.size() >= 2) // observation �迭�� ũ�Ⱑ 2 �̻��� ���
		{
			Vector2d point0, point1;
			int frame_0 = sfm_f[j].observation[0].first; // observation �迭���� 0��° ��� index�� ������
			point0 = sfm_f[j].observation[0].second; // observation �迭���� 0��° ����� point�� ������
			int frame_1 = sfm_f[j].observation.back().first; // observation �迭���� ������ ��� index�� ������
			point1 = sfm_f[j].observation.back().second; // observation �迭���� ������ ����� point�� ������
			Vector3d point_3d;
			triangulatePoint(Pose[frame_0], Pose[frame_1], point0, point1, point_3d); // triangulate ����
			sfm_f[j].state = true; // ���¸� true�� ����
			sfm_f[j].position[0] = point_3d(0); // position�� ������ ������ ����
			sfm_f[j].position[1] = point_3d(1);
			sfm_f[j].position[2] = point_3d(2);
			//cout << "trangulated : " << frame_0 << " " << frame_1 << "  3d point : "  << j << "  " << point_3d.transpose() << endl;
		}		
	}

/*
	for (int i = 0; i < frame_num; i++)
	{
		q[i] = c_Rotation[i].transpose(); 
		cout << "solvePnP  q" << " i " << i <<"  " <<q[i].w() << "  " << q[i].vec().transpose() << endl;
	}
	for (int i = 0; i < frame_num; i++)
	{
		Vector3d t_tmp;
		t_tmp = -1 * (q[i] * c_Translation[i]);
		cout << "solvePnP  t" << " i " << i <<"  " << t_tmp.x() <<"  "<< t_tmp.y() <<"  "<< t_tmp.z() << endl;
	}
*/
	//full BA
	ceres::Problem problem; // Ceres Solver�� ����ȭ ������ �����ϴ� ��ü ���� (Bundle Adjustment�� Ceres Solver��� ���̺귯���� ����ؼ� �ذ�)
	ceres::LocalParameterization* local_parameterization = new ceres::QuaternionParameterization(); //���ʹϾ� ������ ȸ�� ����� ������ �����ϵ��� �����ϴ� ��ü ����
	//cout << " begin full BA " << endl;
	for (int i = 0; i < frame_num; i++) // ��� frame�� ���� ��ȸ
	{
		//double array for ceres
		c_translation[i][0] = c_Translation[i].x(); // c_Translation�� x, y, z ���� Ceres Solver�� ����ϴ� ���·� ��ȯ�ϱ� ���� c_translation�� ����
		c_translation[i][1] = c_Translation[i].y();
		c_translation[i][2] = c_Translation[i].z();
		c_rotation[i][0] = c_Quat[i].w(); // Ceres Solver�� ����ϴ� �迭 ���·� c_Quat�� ����
		c_rotation[i][1] = c_Quat[i].x();
		c_rotation[i][2] = c_Quat[i].y();
		c_rotation[i][3] = c_Quat[i].z();
		problem.AddParameterBlock(c_rotation[i], 4, local_parameterization); //ȸ���� ��ġ �迭�� ����ȭ ������� �߰�
		problem.AddParameterBlock(c_translation[i], 3);
		if (i == l) // i�� l�� ���� ���
		{
			problem.SetParameterBlockConstant(c_rotation[i]); // l �������� ���� ���������� ���, ���� �������� ȸ���� �ٲ��� �ʵ��� ����
		}
		if (i == l || i == frame_num - 1)
		{
			problem.SetParameterBlockConstant(c_translation[i]); // l�� ������ �������� ��ġ ������ ����� ����, ���� �����Ӱ� ������ �������� ��ġ�� �ٲ��� �ʵ��� ����
		}
	}

	for (int i = 0; i < feature_num; i++) // ��� feature point�� ���� �ݺ�
	{
		if (sfm_f[i].state != true) // SFMFeature ��ü�� state�� true�� ��� ����
			continue;
		for (int j = 0; j < int(sfm_f[i].observation.size()); j++) // observation �迭�� ũ�⸸ŭ �ݺ�
		{
			int l = sfm_f[i].observation[j].first; // frame index�� �����´�.
			ceres::CostFunction* cost_function = ReprojectionError3D::Create(
												sfm_f[i].observation[j].second.x(),
												sfm_f[i].observation[j].second.y()); //featue�� x, y ��ǥ�� ���� ������ ������ ����ϴ� CostFunction�� ����

    		problem.AddResidualBlock(cost_function, NULL, c_rotation[l], c_translation[l], 
    								sfm_f[i].position);	 // ������ ������ �ּ�ȭ�ϴ� Residual Block�� �߰�, ī�޶� rotation, ī�޶� translation, position�� ����ȭ �������
		}
		 
	}
	ceres::Solver::Options options; // Ceres Solver�� �ɼ��� �����ϴ� ����ü ����
	options.linear_solver_type = ceres::DENSE_SCHUR; // ����ȭ�� ����� liner_solver_type ����, DENSE_SCHUR Ÿ���� ���
	//options.minimizer_progress_to_stdout = true;
	options.max_solver_time_in_seconds = 0.2; // ����ȭ�� ������ �ִ� �ð��� ����
	ceres::Solver::Summary summary; // ����ȭ ����� ������ ����ü ����
	ceres::Solve(options, &problem, &summary); // Ceres Solver�� ����Ͽ� ����ȭ�� ����
	//std::cout << summary.BriefReport() << "\n";
	// ����ȭ ���� Ÿ���� ceres::CONVERGENCE(����ȭ ����)�̰ų� ����ȭ ���� �� ������ 5e-03���� ���� ���
	if (summary.termination_type == ceres::CONVERGENCE || summary.final_cost < 5e-03)
	{
		//cout << "vision only BA converge" << endl;
	}
	else // ����ȭ ����
	{
		//cout << "vision only BA not converge " << endl;
		return false;
	}
	for (int i = 0; i < frame_num; i++) // ��� �����ӿ� ���� �ݺ�
	{
		q[i].w() = c_rotation[i][0]; // Ceres Solver���� ����ȭ�� rotation ����� q ��Ŀ� ����
		q[i].x() = c_rotation[i][1]; 
		q[i].y() = c_rotation[i][2]; 
		q[i].z() = c_rotation[i][3]; 
		q[i] = q[i].inverse(); //������ ���Ͽ� ���� ���·� ��ȯ (�Լ� �ʱ⿡ q �迭�� ���� inverse ������ �Ͽ� c_Quat�� �����Ͽ��⿡)
		//cout << "final  q" << " i " << i <<"  " <<q[i].w() << "  " << q[i].vec().transpose() << endl;
	}
	for (int i = 0; i < frame_num; i++) // ��� �����ӿ� ���� �ݺ�
	{

		T[i] = -1 * (q[i] * Vector3d(c_translation[i][0], c_translation[i][1], c_translation[i][2])); // ����ȭ�� translation ��Ŀ� ���� q���� ������ ������ ���� ������ ��ġ�� ��� 
		//cout << "final  t" << " i " << i <<"  " << T[i](0) <<"  "<< T[i](1) <<"  "<< T[i](2) << endl;
	}
	for (int i = 0; i < (int)sfm_f.size(); i++)
	{
		if(sfm_f[i].state) // state�� true�� ���
			sfm_tracked_points[sfm_f[i].id] = Vector3d(sfm_f[i].position[0], sfm_f[i].position[1], sfm_f[i].position[2]); //������ ���� ��ǥ�� �����ϴ� map ��ü�� ������ ���� ��ǥ ����
	}
	return true; // true ��ȯ

}


#include "nnlm.h"


//[[Rcpp::export]]
Rcpp::List nmf_nnls(const mat & A, int k, double eta, double beta, int max_iter, double tol, int n_threads, bool show_progress)
{
	/*
	 * Description:
	 * 	Non-negative matrix factorization with penalties using NNLS.
	 * 		argmin_{W>=0, H>=0} ||A - WH||_2^2 + eta*||W||_F^2 + beta*sum(||H.col(j)||_1^2)
	 * Arguments:
	 * 	A: matrix to be factorized as A_{n,m} ~ W_{n,k} * H_{k,m}.
	 * 	k: rank of factorization.
	 * 	eta: L2 penalty on the left (W). Default to no penalty. If eta < 0 then eta = max(A)
	 * 	beta: L1 penalty on the right (H). Default to no penalty.
	 * 	max_iter: maximum iteration of alternating NNLS solutions to H and W
	 * 	tol: stop criterion, maximum difference of target_error between two successive iterations.
	 * Return:
	 * 	A list of 
	 * 		W, H, 
	 * 		error: root mean square error between A and W*H)
	 * 		target_error: root mean(devided by nxm) square error of the target function. Same as error if no penalty.
	 * Methods:
	 * 	Apply `nnls_solver` to W and H alternatingly. `nnls_solver` is implemented using sequential coordinate descend methods.
	 * Author: 
	 * 	Eric Xihui Lin <xihuil.silence@gmail.com>
	 * Version:
	 * 	2015-10-31
	 */

	mat W(A.n_rows, k, fill::randu);
	mat H(k, A.n_cols);
	W = normalise(W);  
	mat WtW(W.n_cols, W.n_cols);
	mat HHt(W.n_rows, H.n_rows);
	// err, pen_err = root mean square error/penalized_error
	vec err(max_iter);
	err.fill(-9999);
	vec pen_err(err);
	if (eta < 0) eta = max(max(A));

	// check progression
	Progress prgrss(max_iter, show_progress);

	// solve H given W
	WtW = W.t()*W;
	if (beta > 0) WtW += beta;
	H = nnls_solver(WtW, -W.t()*A, max_iter, tol, n_threads);
	
	prgrss.increment();

	int i = 0;
	for(; i < max_iter; i++)
	{
		Rcpp::checkUserInterrupt();

		prgrss.increment();

		// solve W given H
		HHt = H*H.t();
		if (eta > 0) HHt.diag() += eta;
		W = nnls_solver(HHt, -H*A.t(), max_iter*(1+i), tol/(1+i), n_threads).t();

		// solve H given W
		WtW = W.t()*W;
		if (beta > 0) WtW += beta;
		H = nnls_solver(WtW, -W.t()*A, max_iter*(1+i), tol/(1+i), n_threads);

		pen_err[i] = mean(mean(square(A - W*H)));
		err[i] = std::sqrt(pen_err[i]);
		if (beta > 0) pen_err[i] += mean(vectorise(square(mean(H)))) * k/A.n_cols;
		if (eta > 0) pen_err[i] += eta * mean(mean(square(W)))*k*k / A.n_rows;
		pen_err[i] = std::sqrt(pen_err[i]);

		if (i > 0 && std::abs(pen_err[i-1] - pen_err[i]) < tol) {
			break;
			}
	}

	if (max_iter <= i)
	{
		//Rcpp::Function warning("warning");
		Rcpp::warning("Target tolerence not reached. Try a larger max.iter.");
	}

	err.resize(i < max_iter ? i+1 : max_iter);
	pen_err.resize(err.n_elem);

	return Rcpp::List::create(
		Rcpp::Named("W") = W, 
		Rcpp::Named("H") = H, 
		Rcpp::Named("error") = arma::sqrt(err),
		Rcpp::Named("target_error") = arma::sqrt(pen_err),
		Rcpp::Named("eta") = eta,
		Rcpp::Named("beta") = beta
		);
}


mat nnls_solver(const mat & H, mat mu, int max_iter, double tol, int n_threads)
{
	/*
	 * Description: sequential Coordinate-wise algorithm for non-negative least square regression problem
	 * 		A x = b, s.t. x >= 0
	 * Arguments:
	 * 	H: A^T * A
	 * 	mu: -A^T * b
	 * 	max_iter: maximum number of iterations.
	 * 	tol: stop criterion, minimum change on x between two successive iteration.
	 * Return: 
	 * 	x: solution to argmin_{x, x>=0} ||Ax - b||_F^2
	 * Reference: 
	 * 	http://cmp.felk.cvut.cz/ftp/articles/franc/Franc-TR-2005-06.pdf 
	 * Author:
	 * 	Eric Xihui Lin <xihuil.silence@gmail.com>
	 * Version:
	 * 	2015-10-31
	 */

	mat x(H.n_cols, mu.n_cols, fill::zeros);
	if (n_threads < 0) n_threads = 0; 

	#pragma omp	parallel for num_threads(n_threads) schedule(dynamic)
	for (int j = 0; j < mu.n_cols; j++)
	{
		vec x0(H.n_cols);
		x0.fill(-9999);
		double tmp;
		int i = 0;
		while(i < max_iter && arma::max(arma::abs(x.col(j) - x0)) > tol)
		{
			x0 = x.col(j);
			for (int k = 0; k < H.n_cols; k++) 
			{
				tmp = x.at(k,j) - mu.at(k,j) / H.at(k,k);
				if (tmp < 0) tmp = 0;
				if (tmp != x.at(k,j)) mu.col(j) += (tmp - x.at(k, j)) * H.col(k);
				x.at(k,j) = tmp;
			}
			++i;
		}
	}

	return x;
}
